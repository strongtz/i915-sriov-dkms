# PVE Host Installation Steps (PVE 9.1)

1. Install build tools: `apt install build-essential dkms`
2. Install the kernel and headers for desired version: `apt install proxmox-default-kernel proxmox-default-headers` (for unsigned kernel).
3. Download deb package from the [releases page](https://github.com/strongtz/i915-sriov-dkms/releases)
   ```sh
   wget -O /tmp/i915-sriov-dkms_2026.02.09_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2026.02.09/i915-sriov-dkms_2026.02.09_amd64.deb"
   ```
4. Install the deb package with dpkg: `dpkg -i /tmp/i915-sriov-dkms_2026.02.09_amd64.deb`
5. Once finished, the kernel commandline needs to be adjusted: `nano /etc/default/grub` and change `GRUB_CMDLINE_LINUX_DEFAULT` to `intel_iommu=on i915.enable_guc=3 i915.max_vfs=7 module_blacklist=xe`, or add to it if you have other arguments there already.
6. You can also use `xe` driver instead of `i915` as described in the [Required Kernel Parameters](https://github.com/strongtz/i915-sriov-dkms?tab=readme-ov-file#required-kernel-parameters) section.
7. Update `grub` and `initramfs` by executing `update-grub` and `update-initramfs -u`
8. Optionally pin the kernel version and update the boot config via `proxmox-boot-tool`.
9. In order to enable the VFs, a `sysfs` attribute must be set. Install `sysfsutils`, then do `echo "devices/pci0000:00/0000:00:02.0/sriov_numvfs = 7" > /etc/sysfs.conf`.
10. Reboot the system.
11. When the system is back up again, you should see the number of VFs under 02:00.1 - 02:00.7.
12. You can passthrough the VFs to LXCs or VMs. However, never pass the **PF (02:00.0)** to **VM** which would crash all other VFs.

## Optional Configurations

### Block VFs on the Host
Leaving VFs exposed to the host can lead to system instability and software conflicts. VFs provide no functional benefit to the host environment; their sole purpose is to serve guest virtual machines. To ensure host stability and performance, it is highly recommended to block these VFs from the host operating system.

To apply this configuration, follow the **[Block VFs Setup Guide](block-vfs.md)**.

### UEFI Secure Boot Enabled Configuration
If secure boot support is required for you, please enable UEFI secure boot before installing i915-sriov-dkms. Note: Only applicable to Ubuntu, PVE, or other distributions based on Debian. Arch Linux users please refer to the Arch Linux Wiki.

To apply this configuration, follow the **[UEFI Secure Boot Setup Guide](secure-boot.md)**.

## Uninstallation

Remove the package with `dpkg -P i915-sriov-dkms`.

If you installed the module manually, or if the package manager fails to remove it from the kernel tree, you can remove it forcibly with:
`dkms remove i915-sriov-dkms/2026.02.09`
