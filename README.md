# Linux i915 and xe driver (dkms module) with SR-IOV support for linux 6.12 ~ 6.19

This repo is a code snapshot of the i915 and xe modules from the mainline linux kernel with SR-IOV support ported from the [intel/mainline-tracking.git](https://github.com/intel/mainline-tracking.git)

**Disclaimer:** This repository is a community project and is **NOT** affiliated with, endorsed by, or connected to Intel Corporation. This kernel module is a port based on the mainline kernel and Intel's implementation and may contain experimental and unstable features. 
Please use it at your own risk.

## Warning

This package is **highly experimental**, you should only use it when you know what you are doing.

You need to install this dkms module in **both host and guest!**

## Required kernel

**Required kernel**: 6.17.x ~ 7.0.x

**Latest release**: [2026.05.06](https://github.com/strongtz/i915-sriov-dkms/releases/tag/2026.05.06)

For older kernel (v6.12 ~ v6.19), please use the [2026.03.05.1](https://github.com/strongtz/i915-sriov-dkms/releases/tag/2026.03.05.1) release.

For v6.8 ~ v6.12, please use the [2025.07.22](https://github.com/strongtz/i915-sriov-dkms/releases/tag/2025.07.22) release.

For v6.1 ~ v6.7, please use [intel-lts-v6.1](https://github.com/strongtz/i915-sriov-dkms/tree/intel-lts-v6.1) branch instead.

It is recommended that to upgrade to a supported kernel, the older branches will no longer be maintained.

**Note**: The Host and Guest VM are not strictly required to use the same module version.

**Note on Secure Boot:** Loading out-of-tree kernel modules requires Secure Boot to be disabled. If you require Secure Boot, ensure you are using a signed kernel and follow the instructions in the [UEFI Secure Boot Enabled Configuration](docs/secure-boot.md) guide to sign the module.

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

**Xe module currently does not support MTL (Meteor Lake) and LNL (Lunar Lake) platforms. Please use i915 instead.**

Replace `${device_id}` with the output from `cat /sys/devices/pci0000:00/0000:00:02.0/device` command

## Manually create Virtual Functions (VFs)

```
echo 7 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on Intel UHD Graphics

## Generic Troubleshooting
If you encounter any issues that prevent the system from booting properly (e.g., a hang or black screen), you can temporarily disable the module by modifying the kernel command line at boot.

### GRUB2
1. At the boot menu, select the desired kernel and press `e` to edit the boot parameters.

1. Locate the line starting with linux and append `module_blacklist=i915,xe` to the end of it (preceded by a space).

1. Press **F10** or **Ctrl+X** to boot.

### systemd-boot

1. Press `e` at the boot menu to edit the kernel parameters.

1. Append `module_blacklist=i915,xe` to the end of the existing line (preceded by a space)

1. Press **Enter** to boot.

Once the system is up, you can inspect the kernel logs to troubleshoot. 
If the module remains unstable, follow the [Uninstallation](#installation-guides) steps in your distribution's installation guide to remove the module.


## Installation Guides

For detailed installation instructions, please refer to the specific guide for your operating system:

### Host Installation
- [Arch Linux Host](docs/install-arch-host.md)
- [NixOS Linux Host (Tested Kernel 6.17)](docs/install-nixos-host.md)
- [Proxmox PVE Host](docs/install-pve-host.md)
- [Manual Host Installation Steps](docs/install-manual.md) - Applicable to Debian, Ubuntu, and Arch Linux hosts.

### Guest Installation
- [Linux Guest (Ubuntu)](docs/install-linux-guest.md)
- [Linux Guest (Ubuntu 25.04 Cloud-Init VM on Proxmox)](docs/install-linux-guest-proxmox-cloud-init.md)
- [Windows Guest (Tested with Proxmox 8.3 + Windows 11 24H2 + Intel Driver 32.0.101.6460/32.0.101.6259)](docs/install-windows-guest.md)

### Additional Configurations
- [Block VFs on the Host (Optional)](docs/block-vfs.md) - Applicable to Arch Linux, Ubuntu, and Proxmox VE hosts.
- [UEFI Secure Boot Enabled Configuration (Optional)](docs/secure-boot.md) - Applicable to Ubuntu, Proxmox VE, and Debian-based distributions.

# Credits

Major contributors to this repository are listed below.

- [@strongtz](https://github.com/strongtz) _Create the initial dkms module_
- [@zhtengw](https://github.com/zhtengw) _Rebase on the linux-intel-lts (v5.15, v6.1) , support for v6.1~v6.4, participation in 15+ issues_
- [@bbaa-bbaa](https://github.com/bbaa-bbaa) _Rebase on the mainline-tracking linux/v6.12 branch, support for v6.8~v6.13, participation in 10+ issues_
- [@pasbec](https://github.com/pasbec) _Major refactor to the repo, support for (v6.2, v6.5, v6.8), participation in 20+ issues_
- [@shenwii](https://github.com/shenwii) _Support for (v6.7, v6.9)_
- [@MotherOfTheGracchi](https://github.com/MotherOfTheGracchi) _Support for v6.5.3_
- [@michael-pptf](https://github.com/michael-pptf) _Several updates to README.md, participation in 20+ issues_
- [@NightMean](https://github.com/NightMean) _Refactored README.md to use OS-specific subpages and added the Proxmox Ubuntu Cloud-Init installation guide_
