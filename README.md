# Linux i915 driver (dkms module) with SR-IOV support for linux 6.8-6.12

This repo is a code snapshot of the i915 module from https://github.com/intel/mainline-tracking/tree/linux/v6.12 and will randomly merge patches from the linux-stable tree.

## Warning

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and guest!**

Tested kernel versions: 6.12.6-zen1/6.11.9-arch1/6.10.9-arch1/6.9.10-arch1/6.8.9-arch1 with ArchLinux


## Required Kernel Parameters
```
intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe
```

## Creating Virtual Functions (VF)

```
echo 2 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on Intel UHD Graphics 

## Arch Linux Installation Steps (Tested Kernel 6.12.6-zen1)

For Arch Linux users, it is available in AUR. [i915-sriov-dkms](https://aur.archlinux.org/packages/i915-sriov-dkms) 

You also can download the package from the [releases page](https://github.com/strongtz/i915-sriov-dkms/releases) and install it with `pacman -U`.

## PVE Host Installation Steps (Tested Kernel 6.8)
1. Install build tools: `apt install build-* dkms`
1. Install the kernel and headers for desired version: `apt install proxmox-headers-6.8 proxmox-kernel-6.8` (for unsigned kernel).
1. Download deb package from the [releases page](https://github.com/strongtz/i915-sriov-dkms/releases)
1. Install the deb package with apt: `apt install ./i915-sriov-dkms_2024.12.30_amd64.deb`
1. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
1. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u`
1. Optionally pin the kernel version and update the boot config via `proxmox-boot-tool`.
1. In order to enable the VFs, a `sysfs` attribute must be set. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
1. Reboot the system.
1. When the system is back up again, you should see the number of VFs under 02:00.1 - 02:00.7. Again, assuming your iGPU is on 00:02 bus.
1. You can passthrough the VFs to LXCs or VMs. However, never touch the PF which is 02:00.0 under any circumstances.

## Linux Guest Installation Steps (Tested Kernel 6.2)
We will need to run the same driver under Linux guests. We can repeat the steps for installing the driver. However, when modifying command line defaults, we use `i915.enable_guc=3` instead of `i915.enable_guc=3 i915.max_vfs=7`. Furthermore, we don't need to use `sysfsutils` to create any more VFs since we ARE using a VF.
Once that's done, update `grub` and `initramfs`, then reboot. Once the VM is back up again, do `dmesg | grep i915` to see if your VF is recognized by the kernel.
Optionally, install `vainfo`, then do `vainfo` to see if the iGPU has been picked up by the VAAPI.
## Windows Guest
It is required to set the host CPU type in Proxmox to "host". I was able to get it working without further fiddling in the config files but your mileage may vary (i5-12500T with UHD 770).
I've used Intel gfx version 4316 to get it working. Here's a link to download it.
(https://www.intel.com/content/www/us/en/download/741626/780560/intel-arc-pro-graphics-windows.html)

See also: https://github.com/strongtz/i915-sriov-dkms/issues/8#issuecomment-1567465036

## Manual Installation Steps
1. Install build tools: `apt install build-essential dkms git` / `pacman -S base-devel dkms git`.
1. Install the kernel and headers for desired version: `apt install linux-headers-$(uname -r)` / `pacman -S linux-headers`.
1. Clone the repository: `git clone https://github.com/strongtz/i915-sriov-dkms.git`.
1. Add the module to DKMS: `dkms add ./i915-sriov-dkms`.
1. Install the module with DKMS: `dkms install i915-sriov-dkms/2024.12.30`.
1. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
1. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u` / for Arch Linux `grub-mkconfig -o /boot/grub/grub.cfg` and `mkinitcpio -P`.
1. Optionally use `sysfsutils` to set the number of VFs on boot. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
1. Reboot the system.

## Uninstallation
### apt
Remove the package with `apt remove i915-sriov-dkms`
### pacman
Remove the package with `pacman -R i915-sriov-dkms`
### manual
Remove the dkms module with `dkms remove i915-sriov-dkms/2024.12.30`

# Credits

Major contributors to this repository are listed below.

* [@strongtz](https://github.com/strongtz) _Create the initial dkms module_
* [@zhtengw](https://github.com/zhtengw) _Rebase on the linux-intel-lts (v5.15, v6.1) , support for v6.1~v6.4, participation in 15+ issues_
* [@bbaa-bbaa](https://github.com/bbaa-bbaa) _Rebase on the mainline-tracking linux/v6.12 branch, support for v6.8~v6.13, participation in 10+ issues_
* [@pasbec](https://github.com/pasbec) _Major refactor to the repo, support for (v6.2, v6.5, v6.8), participation in 20+ issues_
* [@shenwii](https://github.com/shenwii) _Support for (v6.7, v6.9)_
* [@MotherOfTheGracchi](https://github.com/MotherOfTheGracchi) _Support for v6.5.3_
* [@michael-pptf](https://github.com/michael-pptf) _Several updates to README.md, participation in 20+ issues_
