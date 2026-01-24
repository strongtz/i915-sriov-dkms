# Linux i915 and xe driver (dkms module) with SR-IOV support for linux 6.12 ~ 6.18

This repo is a code snapshot of the i915 and xe modules from the mainline linux kernel with SR-IOV support ported from the [intel/mainline-tracking.git](https://github.com/intel/mainline-tracking.git)

**Disclaimer:** This repository is a community port of the mainline kernel and Intel’s mainline-tracking tree.  
It includes some experimental and unstable features and is not an official Intel project.

## Warning

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and guest!**

## Required kernel versions

Required kernel: **6.12.x ~ 6.18.x**

For older kernel (v6.8 ~ v6.12), please use the [2025.07.22](https://github.com/strongtz/i915-sriov-dkms/releases/tag/2025.07.22) release.

For v6.1 ~ v6.7, please use [intel-lts-v6.1](https://github.com/strongtz/i915-sriov-dkms/tree/intel-lts-v6.1) branch instead.

It is recommended that to upgrade to a supported kernel, the older branches will no longer be maintained.

## Required Kernel Parameters

Starting from the current version, this repo provides both the i915 and xe drivers.
You can switch between them by modifying the kernel parameters.

### i915

```
intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe
```

### xe

```
intel_iommu=on xe.max_vfs=7 xe.force_probe=${device_id} module_blacklist=i915
```

**Xe module only supports Gen12(Alder Lake), Gen13(Raptor Lake) and Gen14(Raptor Lake Refresh) for now**

Replace `${device_id}` with the output from `cat /sys/devices/pci0000:00/0000:00:02.0/device` command

## Manually create Virtual Functions (VFs)

```
echo 7 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on Intel UHD Graphics

## Arch Linux Host Installation Steps

1. Install the kernel headers. Note that if you are using a different kernel, replace the package name, such as `linux-headers`, `linux-zen-headers` or `linux-lts-headers`.

2. For Arch Linux users, [i915-sriov-dkms](https://aur.archlinux.org/packages/i915-sriov-dkms) is available in AUR, you can install it with `yay -S i915-sriov-dkms`.
   Or you can download the package from the [Releases Page](https://github.com/strongtz/i915-sriov-dkms/releases) and install it with `pacman -U`.

3. Add required kernel parameters based on your bootloader. You can refer to the Arch Linux Wiki [here](https://wiki.archlinux.org/title/Kernel_parameters#).

4. Regenerate the initramfs images. Such as `sudo mkinitcpio -P`.

5. Create a systemd service file `/etc/systemd/system/i915-sriov.service` to automatically enable Virtual Functions (VFs):

   ```properties
   [Unit]
   Description=Enable Intel i915 SR-IOV VFs
   Before=display-manager.service

   [Service]
   Type=oneshot
   ExecStart=/usr/bin/bash -c 'echo 7 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs'
   RemainAfterExit=yes

   [Install]
   WantedBy=multi-user.target
   ```

   After saving the file, enable this service by running: `sudo systemctl enable i915-sriov.service`

6. Reboot.

## NixOS Linux Host Installation Steps (Tested Kernel 6.17)

For NixOS users, the i915-sriov kernel module can be directly included in your NixOS configuration without the use of DKMS. In particular, the kernel module is provided as a NixOS module that must be included in your NixOS configuration. This NixOS module places the i915-sriov kernel module via an overlay in your `pkgs` attribute set with the attribute name `i915-sriov`. This kernel module can then be included in your configuration by declaring `boot.extraModulePackages = [ pkgs.i915-sriov ];` The same applies also to `xe-sriov`. It is recommened to set `inputs.nixpkgs.follows = "nixpkgs"` to avoid version mismatch.

## PVE Host Installation Steps (PVE 9 with Kernel 6.14)

1. Install build tools: `apt install build-* dkms`
2. Install the kernel and headers for desired version: `apt install proxmox-headers-6.14 proxmox-kernel-6.14` (for unsigned kernel).
3. Download deb package from the [releases page](https://github.com/strongtz/i915-sriov-dkms/releases)
   ```sh
   wget -O /tmp/i915-sriov-dkms_2025.12.10_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2025.12.10/i915-sriov-dkms_2025.12.10_amd64.deb"
   ```
4. Install the deb package with dpkg: `dpkg -i /tmp/i915-sriov-dkms_2025.12.10_amd64.deb`
5. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe`, or add to it if you have other arguments there already.
6. You can also use `xe` driver instead of `i915` as described in the [Required Kernel Parameters](https://github.com/strongtz/i915-sriov-dkms?tab=readme-ov-file#required-kernel-parameters) section.
7. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u`
8. Optionally pin the kernel version and update the boot config via `proxmox-boot-tool`.
9. In order to enable the VFs, a `sysfs` attribute must be set. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`.
10. Reboot the system.
11. When the system is back up again, you should see the number of VFs under 02:00.1 - 02:00.7.
12. You can passthrough the VFs to LXCs or VMs. However, never pass the **PF (02:00.0)** to **VM** which would crash all other VFs.

## Block VFs on the Host (Optional)

While enables VFs, leaving VFs exposed to the host can lead to system instability and software conflicts. In certain desktop environments, hot-plugging or unbinding VFs while assigning them to KVM virtual machines can trigger a complete GUI session crash (See [issue#279](https://github.com/strongtz/i915-sriov-dkms/issues/279)). Many host-side applications that utilize the iGPU (e.g., Media Servers like Emby/Plex or monitoring tools like intel_gpu_top) may incorrectly identify and attempt to use the VFs. This often results in abnormal output, failed hardware transcoding, or initialization errors. VFs provide no functional benefit to the host environment; their sole purpose is to serve guest virtual machines.

To ensure host stability and performance, it is highly recommended to block these VFs from the host operating system. This ensures the host interacts exclusively with the Physical Function (PF) at address 0000:00:02.0, leaving the VFs "invisible" until they are passed through to a guest.

Here are the steps, tested on `Arch Linux`, `Ubuntu`, `PVE` host:

1. Enable `vfio` module:
   ```bash
   echo "vfio-pci" | sudo tee /etc/modules-load.d/vfio.conf
   ```
2. Run `cat /sys/devices/pci0000:00/0000:00:02.0/device` to find your iGPU's Device ID (for example, here is `a7a0`).

3. Create the udev rule: Execute the following command (ensure you replace `a7a0` with your actual Device ID):
   ```bash
   sudo tee /etc/udev/rules.d/99-i915-vf-vfio.rules <<EOF
   # Bind all i915 VFs (00:02.1 to 00:02.7) to vfio-pci
   ACTION=="add", SUBSYSTEM=="pci", KERNEL=="0000:00:02.[1-7]", ATTR{vendor}=="0x8086", ATTR{device}=="0xa7a0", DRIVER!="vfio-pci", RUN+="/bin/sh -c 'echo \$kernel > /sys/bus/pci/devices/\$kernel/driver/unbind; echo vfio-pci > /sys/bus/pci/devices/\$kernel/driver_override; modprobe vfio-pci; echo \$kernel > /sys/bus/pci/drivers/vfio-pci/bind'"
   EOF
   ```
4. Regenerate the initramfs images. Such as `sudo mkinitcpio -P` on Arch Linux, `sudo update-initramfs -u` on Ubuntu or other Debian-based distributions.

5. Reboot.
6. Use the lspci command to check which driver is currently in use for the VFs (00:02.1 through 00:02.7).

   ```bash
   lspci -nnk -s 00:02
   ```

   Expected Output: The Physical Function (00:02.0) should still use the `i915` or `xe` driver, while all Virtual Functions should display Kernel driver in use: `vfio-pci`.

   ```bash
   00:02.0 VGA compatible controller: Intel Corporation ... (rev 0c)
         Kernel driver in use: i915
   ...
   00:02.1 Video device: Intel Corporation ... (rev 0c)
         Kernel driver in use: vfio-pci
   00:02.2 Video device: Intel Corporation ... (rev 0c)
         Kernel driver in use: vfio-pci
   ```

   And check if any render nodes exist for the VFs in /dev/dri/.

   ```bash
   ls /dev/dri/
   ```

   You should typically only see card0 and renderD128. If you see a long list (e.g., renderD130 through renderD135), the VFs have not been blocked successfully.

## UEFI Secure Boot Enabled Configuration (Optional)

Note: Only applicable to Ubuntu, PVE, or other distributions based on Debian. If secure boot support is required for you, please enable UEFI secure boot before installing i915-sriov-dkms. For PVE, it is important to ensure that secure boot is enabled when installing PVE, otherwise in some ZFS based installations, a kernel that is not signed may be installed by default, which cannot support secure boot. In this situation, it is necessary to first refer to the PVE documentation to configure secure boot. Arch Linux users please refer to the [Arch Linux Wiki](https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#shim).

The precondition for executing the following steps: `UEFI Secure Boot is turned on and in User/Standard Mode.`

1. Install `mokutill`.
   ```bash
   sudo apt update && sudo apt install mokutil
   ```
2. Install i915-sriov-dkms according to the usual steps.
3. After installation, before reboot the system, execute this command:

   ```bash
   sudo mokutil --import /var/lib/dkms/mok.pub
   ```

   At this point, you will be prompted to enter a password, which is a temporary password that will only be used once during the reboot process.

4. Reboot your computer. Monitor the boot process and wait for the Perform MOK Management window (a blue screen). If you miss the window, you will need to re-run the mokutil command and reboot again. The i915-sriov-dkms module will NOT load until you step through this setup. Select `Enroll MOK`, `Continue`, `Yes`, Enter the temporary password in Step 3, `Reboot`.

## Linux Guest Installation Steps (Ubuntu 25.04/Kernel 6.14)

We will need to run the same driver under Linux guests.

1. Install build tools
   ```
   apt install build-* dkms linux-headers-$(uname -r)
   ```
2. Download and install the `.deb`
   ```
   wget -O /tmp/i915-sriov-dkms_2025.12.10_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2025.12.10/i915-sriov-dkms_2025.12.10_amd64.deb"
   dpkg -i /tmp/i915-sriov-dkms_2025.12.10_amd64.deb
   ```
3. Update kernel parameters
   `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `i915.enable_guc=3 module_blacklist=xe`, or add to it if you have other arguments there already.

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
10. You'll need to copy this file to `/usr/share/kvm` directory on Proxmox host. I uploaded it to a NAS and downloaded it with `wget`.

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

## Manual Installation Steps

1. Install build tools: `apt install build-essential dkms git` / `pacman -S base-devel dkms git`.
1. Install the kernel and headers for desired version: `apt install linux-headers-$(uname -r)` / `pacman -S linux-headers`.
1. Clone the repository: `git clone https://github.com/strongtz/i915-sriov-dkms.git`.
1. Add the module to DKMS: `dkms add ./i915-sriov-dkms`.
1. Install the module with DKMS: `dkms install i915-sriov-dkms/2025.12.10`.
1. If you have secureboot enabled, and `dkms install` tells you it created a self-signed certificate for MOK and you have not installed the host's certificate yet, install the certificate generated by dkms with `mokutil --import /var/lib/dkms/mok.pub`.
1. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7`, or add to it if you have other arguments there already.
1. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u` / for Arch Linux `grub-mkconfig -o /boot/grub/grub.cfg` and `mkinitcpio -P`.
1. Optionally use `sysfsutils` to set the number of VFs on boot. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`,
1. Reboot the system.

## Uninstallation

### dpkg

Remove the package with `dpkg -P i915-sriov-dkms`

### pacman

Remove the package with `pacman -R i915-sriov-dkms`

### manual

Remove the dkms module with `dkms remove i915-sriov-dkms/2025.12.10`

# Credits

Major contributors to this repository are listed below.

- [@strongtz](https://github.com/strongtz) _Create the initial dkms module_
- [@zhtengw](https://github.com/zhtengw) _Rebase on the linux-intel-lts (v5.15, v6.1) , support for v6.1~v6.4, participation in 15+ issues_
- [@bbaa-bbaa](https://github.com/bbaa-bbaa) _Rebase on the mainline-tracking linux/v6.12 branch, support for v6.8~v6.13, participation in 10+ issues_
- [@pasbec](https://github.com/pasbec) _Major refactor to the repo, support for (v6.2, v6.5, v6.8), participation in 20+ issues_
- [@shenwii](https://github.com/shenwii) _Support for (v6.7, v6.9)_
- [@MotherOfTheGracchi](https://github.com/MotherOfTheGracchi) _Support for v6.5.3_
- [@michael-pptf](https://github.com/michael-pptf) _Several updates to README.md, participation in 20+ issues_
