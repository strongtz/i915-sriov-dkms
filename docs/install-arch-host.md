# Arch Linux Host Installation Steps

1. Install the kernel headers. Note that if you are using a different kernel, replace the package name, such as `linux-headers`, `linux-zen-headers` or `linux-lts-headers`.

2. For Arch Linux users, [i915-sriov-dkms](https://aur.archlinux.org/packages/i915-sriov-dkms) is available in AUR, you can install it with `yay -S i915-sriov-dkms`.
   Or you can download the package from the [Releases Page](https://github.com/strongtz/i915-sriov-dkms/releases) and install it with `pacman -U`.

3. Add required kernel parameters based on your bootloader. You can refer to the Arch Linux Wiki [here](https://wiki.archlinux.org/title/Kernel_parameters#).

4. Regenerate the initramfs images. Such as `sudo mkinitcpio -P`.

5. Optionally use `systemd-tmpfiles` to set the number of VFs on boot. The package includes a configuration file at `/etc/tmpfiles.d/i915-set-sriov-numvfs.conf`. Edit it and uncomment the line, changing the argument to the number of VFs you want:

   ```
   w /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs -    -    -    -   7
   ```

6. Reboot.

## Optional Configurations

### Block VFs on the Host
Leaving VFs exposed to the host can lead to system instability and software conflicts. VFs provide no functional benefit to the host environment; their sole purpose is to serve guest virtual machines. To ensure host stability and performance, it is highly recommended to block these VFs from the host operating system.

To apply this configuration, follow the **[Block VFs Setup Guide](block-vfs.md)**.

## Uninstallation

Remove the package with `pacman -R i915-sriov-dkms`.

If you installed the module manually, or if the package manager fails to remove it from the kernel tree, you can remove it forcibly with:
`dkms remove i915-sriov-dkms/2026.02.09`
