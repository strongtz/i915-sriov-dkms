# Linux i915 driver with SR-IOV support (dkms module) for linux 6.1 and linux 6.2

Originally from [linux-intel-lts](https://github.com/intel/linux-intel-lts/tree/lts-v5.15.49-adl-linux-220826T092047Z/drivers/gpu/drm/i915)
Update to [6.1.8](https://github.com/intel/linux-intel-lts/tree/lts-v6.1.8-linux-230201T082419Z/drivers/gpu/drm/i915)

## Notice

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and VM!**

For Arch Linux users, it is available in AUR. [i915-sriov-dkms-git](https://aur.archlinux.org/packages/i915-sriov-dkms-git)

Tested kernel versions: 

* `pve-kernel-6.1.0-1-pve`~`6.1.10-1-pve` on PVE VM Host
* `gentoo-sources-6.1.0-gentoo`~`6.2.0-gentoo` on Gentoo VM Guest

Tested usages:

- VA-API video acceleration in VM (need to remove any other display device such as virtio-gpu)

## My testing cmdline

```
intel_iommu=on i915.enable_guc=7
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
4. Move the entire content of the repository to `/usr/src/i915-sriov-dkms`. The folder name will be the DKMS package name.
5. Execute command `dkms -i -m i915-sriov-dkms -v 6.1`. `-m` argument denotes the package name, and it should be the same as the folder name which contains the package content. `-v` argument denotes the package version, which we have specified in the `dkms.conf` as `6.1`
6. The kernel module should begin building.
7. Once finished, we need to make a few changes to the kernel commandline. `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to 'intel_iommu=on i915.enable_guc=7`, or add to it if you have other arguments there already.
8. Update `grub` and `initrramfs` by executing `update-grub` and `update-initramfs -u`
9. In order to enable the VFs, we need to modify some variables in the `sysfs`. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
10. Reboot the system.
11. When the system is back up again, you should see the number of VFs you specified show up under 02:00.1 - 02:00.7. Again, assuming your iGPU is on 00:02 bus.
12. You can passthrough the VFs to LXCs or VMs. However, never touch the PF which is 02:00.0 under any circumstances.

## Linux Guest Installation Steps (Tested Kernel 6.2)
We will need to run the same driver under Linux guests. We can repeat the steps for installing the driver. However, when modifying command line defaults, we use `i915.enable_guc=3` instead of `i915.enable_guc=7`. Furthermore, we don't need to use `sysfsutils` to create any more VFs since we ARE using a VF.
Once that's done, update `grub` and `initramfs`, then reboot. Once the VM is back up again, do `dmesg | grep i915` to see if your VF is recognized by the kernel.
Optionally, install `vainfo`, then do `vainfo` to see if the iGPU has been picked up by the VAAPI.
