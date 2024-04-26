# Linux i915 driver with SR-IOV support (dkms module) for linux 6.1 ~ linux 6.5

Originally from [linux-intel-lts](https://github.com/intel/linux-intel-lts/tree/lts-v5.15.49-adl-linux-220826T092047Z/drivers/gpu/drm/i915)
Update to [6.1.12](https://github.com/intel/linux-intel-lts/tree/lts-v6.1.12-linux-230415T124447Z/drivers/gpu/drm/i915)

## PVE 8.2 and Kernel 6.8
At the time of writing (Apr 26 2024), this dkms will not work with Kernel 6.8, so you should consider pinning the kernel with `proxmox-boot-tool kernel pin` before proceeding with the upgrade. You can also list all available kernels with `proxmox-boot-tool kernel list`.

## Update Notice

The SR-IOV enablement commandline is changed since [commit #092d1cf](https://github.com/strongtz/i915-sriov-dkms/commit/092d1cf126f31eca3c1de4673e537c3c5f1e6ab4). If you are updating from previous version, please modify `i915.enable_guc=7` to **`i915.enable_guc=3 i915.max_vfs=7`** in your kernel command line.

## Notice

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and VM!**

For Arch Linux users, it is available in AUR. [i915-sriov-dkms-git](https://aur.archlinux.org/packages/i915-sriov-dkms-git)

Tested kernel versions: 

* `pve-kernel-6.1.0-1-pve`~`6.2.9-1-pve` on PVE VM Host
* `gentoo-sources-6.1.19-gentoo`~`6.2.11-gentoo` on Gentoo VM Guest

Tested usages:

- VA-API video acceleration in VM (need to remove any other display device such as virtio-gpu)

## My testing cmdline

```
intel_iommu=on i915.enable_guc=3 i915.max_vfs=7
```

## Creating virtual functions

```
echo 2 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on UHD Graphics 770

## PVE Host Installation Steps (Tested Kernel 6.1 and 6.2) 
1. Clone this repo
2. Install some tools. `apt install build-* dkms`
3. Go inside the repo, edit the `dkms.conf`file, change the `PACKAGE_NAME` to `i915-sriov-dkms`, and change the `PACKAGE_VERSION` to `6.1`. Save the file.
4. Move the entire content of the repository to `/usr/src/i915-sriov-dkms-6.1`. The folder name will be the DKMS package name.
5. Execute command `dkms install -m i915-sriov-dkms -v 6.1 --force`. `-m` argument denotes the package name, and it should be the same as the folder name which contains the package content. `-v` argument denotes the package version, which we have specified in the `dkms.conf` as `6.1`. `--force` argument will reinstall the module even if a module with same name has been already installed.
6. The kernel module should begin building.
7. Once finished, we need to make a few changes to the kernel commandline. `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to 'intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
8. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u`
9. In order to enable the VFs, we need to modify some variables in the `sysfs`. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
10. Reboot the system.
11. When the system is back up again, you should see the number of VFs you specified show up under 02:00.1 - 02:00.7. Again, assuming your iGPU is on 00:02 bus.
12. You can passthrough the VFs to LXCs or VMs. However, never touch the PF which is 02:00.0 under any circumstances.

## Linux Guest Installation Steps (Tested Kernel 6.2)
We will need to run the same driver under Linux guests. We can repeat the steps for installing the driver. However, when modifying command line defaults, we use `i915.enable_guc=3` instead of `i915.enable_guc=3 i915.max_vfs=7`. Furthermore, we don't need to use `sysfsutils` to create any more VFs since we ARE using a VF.
Once that's done, update `grub` and `initramfs`, then reboot. Once the VM is back up again, do `dmesg | grep i915` to see if your VF is recognized by the kernel.
Optionally, install `vainfo`, then do `vainfo` to see if the iGPU has been picked up by the VAAPI.

## Windows Guest
It is required to set the host CPU type in Proxmox to "host". I was able to get it working without further fiddling in the config files but your mileage may vary (i5-12500T with UHD 770).
I've used Intel gfx version 4316 to get it working. Here's a link to download it.
(https://www.intel.com/content/www/us/en/download/741626/780560/intel-arc-pro-graphics-windows.html)

## Debian Guest Installation

Debian poses some additional challenges because not all of the necessary modules are compiled in by default. That leads us to building a custom kernel with a modified version of their configuration. The following steps were tested on Debian 11.6 "bullseye" which includes the 5.10.0 kernel out-of-the-box. We need, at minimum, the 6.1 kernel

First, the VM configuration:
* BIOS: OVMF (UEFI)
* Display: Default
* Machine: q35
* Secure boot must be disabled in the UEFI BIOS, otherwise the new, unsigned, kernel will not start.

With the 6.1 kernel not being available in the stable repository, the testing repository must be added. The following steps are based on [these instructions](https://serverfault.com/questions/22414/how-can-i-run-debian-stable-but-install-some-packages-from-testing)

**NOTE:** All of these commands were run by the root user. Run as a regular
user by prepending `sudo`, if you prefer.

* Create `/etc/apt/preferences.d/stable.pref`:
```
cat <<EOT >> /etc/apt/preferences.d/stable.pref
# 500 <= P < 990: causes a version to be installed unless there is a
# version available belonging to the target release or the installed
# version is more recent
Package: *
Pin: release a=stable
Pin-Priority: 900
EOT
```
* Create `/etc/apt/preferences.d/testing.pref`:
```
cat <<EOT >> /etc/apt/preferences.d/testing.pref
# 100 <= P < 500: causes a version to be installed unless there is a
# version available belonging to some other distribution or the installed
# version is more recent
Package: *
Pin: release a=testing
Pin-Priority: 400
EOT
```
* Move `/etc/apt/sources.list` to `/etc/apt/sources.list.d/stable.list`
```
mv /etc/apt/sources.list /etc/apt/sources.list.d/stable.list
```
* Create `/etc/apt/sources.list.d/testing.list` as follows:
```
sed 's/bullseye/testing/g' /etc/apt/sources.list.d/stable.list > /etc/apt/sources.list.d/testing.list
```

Now the process of building and installing a new kernel begins

* Use `apt` to fully update the system.
```
apt update && apt -y dist-upgrade && apt -y autoremove
```
* Find the latest version of the 6.1 kernel and install it
```
apt search '^linux-image-6.*-amd64'
  linux-image-6.1.0-7-amd64/testing 6.1.20-1 amd64
    Linux 6.1 for 64-bit PCs (signed)
apt -y install linux-image-6.1.0-7-amd64
reboot
```
* Install the 6.1 kernel source and configure it.
```
apt -y install dkms dwarves git linux-source-6.1 pahole vainfo
cd /usr/src
tar xJvf linux-source-6.1.tar.xz
```
* Copy Debian's original build configuration into the source tree:
```
cp /boot/config-6.1.*-amd64 /usr/src/linux-source-6.1/.config
```
* Edit `/usr/src/linux-source-6.1/.config` and ensure the following parameters exist:
```
CONFIG_INTEL_MEI_PXP=m
CONFIG_DRM_I915_PXP=y
```
* Build and install the kernel
```
cd /usr/src/linux-source-6.1
make deb-pkg LOCALVERSION=-sriov KDEB_PKGVERSION=$(make kernelversion)-1

    ...four hours later...

dpkg -i /usr/src/*.deb
reboot
```
* Verify the new kernel is indeed running:
```
uname -r
6.1.15-sriov
```
* Build and install the i915-sriov module
```
cd /usr/src
git clone https://github.com/strongtz/i915-sriov-dkms i915-sriov-dkms-6.1

    edit /usr/src/i915-sriov-dkms-6.1/dkms.conf with the following:
    PACKAGE_NAME="i915-sriov-dkms"
    PACKAGE_VERSION="6.1"

dkms install --force -m i915-sriov-dkms -v 6.1

    edit /etc/default/grub with the following:
    GRUB_CMDLINE_LINUX_DEFAULT="quiet i915.enable_guc=3"

update-grub
update-initramfs -u
poweroff
```
* In Proxmox, add one of the 0000:00:02.x devices to the VM, then start the VM
* Log into the machine and verify that the module has loaded
```
# lspci | grep -i vga
06:10.0 VGA compatible controller: Intel Corporation Alder Lake-S GT1 [UHD Graphics 730] (rev 0c)

# lspci -vs 06:10.0
06:10.0 VGA compatible controller: Intel Corporation Alder Lake-S GT1 [UHD Graphics 730] (rev 0c) (prog-if 00 [VGA controller])
	Subsystem: ASRock Incorporation Alder Lake-S GT1 [UHD Graphics 730]
	Physical Slot: 16-2
	Flags: bus master, fast devsel, latency 0, IRQ 42
	Memory at c1000000 (64-bit, non-prefetchable) [size=16M]
	Memory at 800000000 (64-bit, prefetchable) [size=512M]
	Capabilities: [ac] MSI: Enable+ Count=1/1 Maskable+ 64bit-
	Kernel driver in use: i915
	Kernel modules: i915

# dmesg | grep i915
[    6.461702] i915: loading out-of-tree module taints kernel.
[    6.462463] i915: module verification failed: signature and/or required key missing - tainting kernel
[    6.591228] i915 0000:06:10.0: Running in SR-IOV VF mode
[    6.592001] i915 0000:06:10.0: GuC interface version 0.1.0.0
[    6.592212] i915 0000:06:10.0: [drm] VT-d active for gfx access
[    6.592225] i915 0000:06:10.0: [drm] Using Transparent Hugepages
[    6.593437] i915 0000:06:10.0: GuC interface version 0.1.0.0
[    6.593564] i915 0000:06:10.0: GuC firmware PRELOADED version 1.0 submission:SR-IOV VF
[    6.593565] i915 0000:06:10.0: HuC firmware PRELOADED
[    6.595883] i915 0000:06:10.0: [drm] Protected Xe Path (PXP) protected content support initialized
[    6.595886] i915 0000:06:10.0: [drm] PMU not supported for this GPU.
[    6.595951] [drm] Initialized i915 1.6.0 20201103 for 0000:06:10.0 on minor 1

# ls /dev/dri/render*
crw-rw---- 1 root render 226, 128 Jan 00 00:00 renderD128

# vainfo
libva info: VA-API version 1.17.0
libva info: Trying to open /usr/lib/x86_64-linux-gnu/dri/iHD_drv_video.so
libva info: Found init function __vaDriverInit_1_17
libva info: va_openDriver() returns 0
vainfo: VA-API version: 1.17 (libva 2.12.0)
vainfo: Driver version: Intel iHD driver for Intel(R) Gen Graphics - 23.1.1 ()
vainfo: Supported profile and entrypoints

    ... List of all the compression formats and profiles ...
```
* The kernel DEB installation files can be copied to other, similar, Debian systems for use without recompiling.
