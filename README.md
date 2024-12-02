# Linux i915 driver (dkms module) with SR-IOV support for linux 6.8 ~ linux 6.12

Originally from https://github.com/intel/linux-intel-lts/tree/lts-v6.6.58-linux-241108T122510Z


## Warning

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and guest!**

For Arch Linux users, it is available in AUR. [i915-sriov-dkms](https://aur.archlinux.org/packages/i915-sriov-dkms-git)

Tested kernel versions: 

* Proxmox VE Host: `pve-kernel-6.8.12-4-pve`
* ArchLinux Host: `6.11.5-zen1` `6.12.1-zen1`

## Required Kernel Parameters
```
intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe
```

## Creating Virtual Functions (VF)

```
echo 2 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on Intel UHD Graphics 

## PVE Host Installation Steps (Tested Kernel 6.5 and 6.8)
1. Clone this repo
1. Install build tools: `apt install build-* dkms`
1. Install the kernel and headers for desired version: `apt install proxmox-headers-6.8.8-2-pve proxmox-kernel-6.8.8-2-pve` (for unsigned kernel).
1. If running a distro based on Ubuntu or Debian other than Proxmox, add `-DRELEASE_UBUNTU=1` or `-DRELEASE_DEBIAN=1` respectively to EXTRA_CFLAGS in the Makefile. 
1. Change into the root of the cloned repository and run `dkms add .`.
1. Execute the command `dkms install -m i915-sriov-dkms -v 2024.12.02 --force` or `dkms install -m i915-sriov-dkms -v $(cat VERSION) --force` for a version-independent command.
1. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
1. Optionally pin the kernel version and update the boot config via `proxmox-boot-tool`.
1. In order to enable the VFs, a `sysfs` attribute must be set. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
1. Reboot the system.
1. When the system is back up again, you should see the number of VFs under 02:00.1 - 02:00.7. Again, assuming your iGPU is on 00:02 bus.
1. You can passthrough the VFs to LXCs or VMs. However, never touch the PF which is 02:00.0 under any circumstances.

## PVE Host Installation Steps (Tested Kernel 6.1 and 6.2) 
1. Clone this repo
1. Install some tools. `apt install build-* dkms`
1. Go inside the repo, edit the `dkms.conf`file, change the `PACKAGE_NAME` to `i915-sriov-dkms`, and change the `PACKAGE_VERSION` to `6.1`. Save the file.
1. Move the entire content of the repository to `/usr/src/i915-sriov-dkms-6.1`. The folder name will be the DKMS package name.
1. Execute command `dkms install -m i915-sriov-dkms -v 6.1 --force`. `-m` argument denotes the package name, and it should be the same as the folder name which contains the package content. `-v` argument denotes the package version, which we have specified in the `dkms.conf` as `6.1`. `--force` argument will reinstall the module even if a module with same name has been already installed.
1. The kernel module should begin building.
1. Once finished, we need to make a few changes to the kernel commandline. `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to 'intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
1. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u`
1. In order to enable the VFs, we need to modify some variables in the `sysfs`. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
1. Reboot the system.
1. When the system is back up again, you should see the number of VFs you specified show up under 02:00.1 - 02:00.7. Again, assuming your iGPU is on 00:02 bus.
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

