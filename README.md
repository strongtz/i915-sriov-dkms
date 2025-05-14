# Linux i915 driver (dkms module) with SR-IOV support for linux 6.8-6.15(-rc5)

This repo is a code snapshot of the i915 module from https://github.com/intel/mainline-tracking/tree/linux/v6.12 and will randomly merge patches from the linux-stable tree.

## Warning

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and guest!**

Tested kernel versions: 6.12.10-zen1/6.11.9-arch1/6.10.9-arch1/6.9.10-arch1/6.8.9-arch1 with ArchLinux


## Required Kernel Parameters
```
intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe
```

Besides `intel_iommu=on`, the other 3 parameters could be applied by `modprobe` by putting following content to `/etc/modprobe.d/i915-sriov-dkms.conf`

```
blacklist xe
options i915 enable_guc=3
options i915 max_vfs=7
```

## Creating Virtual Functions (VF)

```
echo 1 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on Intel UHD Graphics 

## Arch Linux Installation Steps (Tested Kernel 6.12.6-zen1)

For Arch Linux users, it is available in AUR. [i915-sriov-dkms](https://aur.archlinux.org/packages/i915-sriov-dkms) 

You also can download the package from the [releases page](https://github.com/strongtz/i915-sriov-dkms/releases) and install it with `pacman -U`.

## PVE Host Installation Steps (Tested Kernel 6.8)
1. Install build tools: `apt install build-* dkms`
1. Install the kernel and headers for desired version: `apt install proxmox-headers-6.8 proxmox-kernel-6.8` (for unsigned kernel).
1. Download deb package from the [releases page](https://github.com/strongtz/i915-sriov-dkms/releases)
   ```sh
   wget -O /tmp/i915-sriov-dkms_2025.05.11_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2025.05.11/i915-sriov-dkms_2025.05.11_amd64.deb"
   ```
1. Install the deb package with dpkg: `dpkg -i /tmp/i915-sriov-dkms_2025.05.11_amd64.deb`
1. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe`, or add to it if you have other arguments there already.
1. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u`
1. Optionally pin the kernel version and update the boot config via `proxmox-boot-tool`.
1. In order to enable the VFs, a `sysfs` attribute must be set. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
1. Reboot the system.
1. When the system is back up again, you should see the number of VFs under 02:00.1 - 02:00.7. Again, assuming your iGPU is on 00:02 bus.
1. You can passthrough the VFs to LXCs or VMs. However, never pass the **PF (02:00.0)** to **VM** which would crash all other VFs.

## Linux Guest Installation Steps (Tested Ubuntu 24.04/Kernel 6.8)
We will need to run the same driver under Linux guests. 
1. Install build tools
   ```
   apt install build-* dkms linux-headers-$(uname -r) linux-modules-extra-$(uname -r)
   ```
2. Download and install the `.deb`
   ```
   wget -O /tmp/i915-sriov-dkms_2025.05.11_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2025.05.11/i915-sriov-dkms_2025.05.11_amd64.deb"
   dpkg -i /tmp/i915-sriov-dkms_2025.05.11_amd64.deb
   ```
3. Update kernel parameters
   `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `i915.enable_guc=3 module_blacklist=xe`, or add to it if you have other arguments there already.

   Example:
   ```conf
   GRUB_CMDLINE_LINUX_DEFAULT="intel_iommu=on i915.enable_guc=3 module_blacklist=xe"
   ```

4. Once that's done, update `grub` and `initramfs`, then reboot.
   ```
   update-grub
   update-initramfs -u
   ```
5. Once the VM is back up again, do `dmesg | grep i915` to see if your VF is recognized by the kernel. You should also check if `xe` is blacklisted correctly by running `lspci -nnk` to see which driver is in use by the VF.
6. Optionally, install `vainfo` by running `apt install vainfo`, then do `vainfo` to see if the iGPU has been picked up by the VAAPI.
7. If OpenCL is desired:
   ```
   apt install intel-opencl-icd
   apt install clinfo
   clinfo
   ```
## Windows Guest (Tested with Proxmox 8.3 + Windows 11 24H2 + Intel Driver 32.0.101.6460/32.0.101.6259)
Thanks for [resiliencer](https://github.com/resiliencer) and his contribution in [#225](https://github.com/strongtz/i915-sriov-dkms/issues/225#issue-2687672590).

These steps ensure compatibility across all driver versions. In theory you can install any version and won't be hit by the dreaded `Code 43`.

### Extract Graphics EFI Firmware
1. Download [UEFITools](https://github.com/LongSoft/UEFITool/releases) (`UEFITool_NE_A68_win64` for Windows. They supply Linux and Mac binaries, too)
2. Download BIOS for motherboard (I suspect any motherboard BIOS would work as long as it is for Alder/Raptop Lake Desktop Platform)
3. Unzip BIOS
4. Use UEFITools (Run as Admin) to load the BIOS (usually `.cap`)
5. Go to `Action - Search` or use keyboard shortcut `ctrl+F` and search for Hex string `49006e00740065006c00280052002900200047004f0050002000440072006900760065007200`
6. Double click the search result in the search column, it will highlight the item found within the BIOS.
7. Right click on the highlighted result and `Extract body...`
8. Save the file, file name and extension do not matter. I used `intelgopdriver_desktop` and it would save as `intelgopdriver_desktop.bin`.
9. You can also compare the checksum of the file:
	1. Windows Terminal Command: `Get-FileHash -Path "path-to-rom" -Algorithm SHA256`
	2. For desktop with UHD730 and UHD770: `131c32cadb6716dba59d13891bb70213c6ee931dd1e8b1a5593dee6f3a4c2cbd`
	3. For ADL-N: `FA12486D93BEE383AD4D3719015EFAD09FC03352382F17C63DF10B626755954B`
11. You'll need to copy this file to `/usr/share/kvm` directory on Proxmox host. I uploaded it to a NAS and downloaded it with `wget`.

### Windows VM Creation
1. When setting up the machine, set `CPU` as `host`.
2. TIPS: You can skip the Microsoft Account setup by pressing `Shift+F10` at the first setup screen and type `OOBE\BYPASSNRO`. The VM will reboot, and you can choose "I don't have Internet" option to set up a local account. Alternatively, you can remove the network device from the Windows VM.
3. When the setup process is done and you are at the Desktop, enable Remote Desktop and make sure your local account user has access. You can shut down the VM for now.
4. When the VM powered off, edit the configuration file:```
```
# Passing the 02.1 VF, specify the romfile. ROM path is relative

hostpci0: 0000:00:02.1,pcie=1,romfile=Intelgopdriver_desktop.efi,x-vga=1
```
5. In the `Hardware` tab, set `Display` to `none`.
6. Start the VM. You won't be able to access it with console, so your only way in is Remote Desktop. Once you are in, download the graphics driver from Intel, any version should work.
7. During install, you may experience black screen when the actual graphics drivers are being installed and applied. This black screen will persist until you restart the VM. My advice is give it a few minutes to do its thing. You can make your best judgement by looking at the VM CPU usage in Proxmox.
8. After rebooting, connect with RDP once again. Go to Device Manager and verify the result. You should see the Intel Graphics is installed and working.

![CleanShot 2025-01-27 at 12 26 28](https://github.com/user-attachments/assets/7e48877f-2b57-42ac-bd0b-c1aa72bddc40)

See also: https://github.com/strongtz/i915-sriov-dkms/issues/8#issuecomment-1567465036

## Manual Installation Steps
1. Install build tools: `apt install build-essential dkms git` / `pacman -S base-devel dkms git`.
1. Install the kernel and headers for desired version: `apt install linux-headers-$(uname -r)` / `pacman -S linux-headers`.
1. Clone the repository: `git clone https://github.com/strongtz/i915-sriov-dkms.git`.
1. Add the module to DKMS: `dkms add ./i915-sriov-dkms`.
1. Install the module with DKMS: `dkms install i915-sriov-dkms/2025.05.11`.
1. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
1. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u` / for Arch Linux `grub-mkconfig -o /boot/grub/grub.cfg` and `mkinitcpio -P`.
1. Optionally use `sysfsutils` to set the number of VFs on boot. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`, assuming your iGPU is on 00:02 bus. If not, use `lspci | grep VGA` to find the PCIe bus your iGPU is on.
1. Reboot the system.

## Uninstallation
### dpkg
Remove the package with `dpkg -P i915-sriov-dkms`
### pacman
Remove the package with `pacman -R i915-sriov-dkms`
### manual
Remove the dkms module with `dkms remove i915-sriov-dkms/2025.05.11`

# Credits

Major contributors to this repository are listed below.

* [@strongtz](https://github.com/strongtz) _Create the initial dkms module_
* [@zhtengw](https://github.com/zhtengw) _Rebase on the linux-intel-lts (v5.15, v6.1) , support for v6.1~v6.4, participation in 15+ issues_
* [@bbaa-bbaa](https://github.com/bbaa-bbaa) _Rebase on the mainline-tracking linux/v6.12 branch, support for v6.8~v6.13, participation in 10+ issues_
* [@pasbec](https://github.com/pasbec) _Major refactor to the repo, support for (v6.2, v6.5, v6.8), participation in 20+ issues_
* [@shenwii](https://github.com/shenwii) _Support for (v6.7, v6.9)_
* [@MotherOfTheGracchi](https://github.com/MotherOfTheGracchi) _Support for v6.5.3_
* [@michael-pptf](https://github.com/michael-pptf) _Several updates to README.md, participation in 20+ issues_
