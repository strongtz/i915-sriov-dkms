# Linux Guest Installation Steps (Ubuntu 25.04 VM via ProxmoxVE Community Scripts / Cloud-Init)

Thanks to [@NightMean](https://github.com/NightMean) for his contribution in [#418](https://github.com/strongtz/i915-sriov-dkms/issues/418).

If you have tried to run the DKMS module on an Ubuntu 25.04 VM created from ProxmoxVE Community scripts (or any minimal/cloud-init Ubuntu image), you might encounter an `Unknown symbol in module` error and silently fail to load `i915`.

This is because the cloud-init/minimal Ubuntu image does not come with the extra kernel modules installed by default. The `i915-sriov-dkms` module depends on DRM helper functions `drm_display_helper` and `drm_kms_helper`.

To fix this, you must install `linux-modules-extra` alongside the typical build tools.

### 1. Install required packages
Instead of installing only `linux-headers`, you need to install `linux-modules-extra` as well:
```bash
sudo apt install -y build-essential dkms linux-headers-$(uname -r) linux-modules-extra-$(uname -r)
```

### 2. Download and install the DKMS module
Proceed with the same commands as in the standard Readme:
```bash
wget -O /tmp/i915-sriov-dkms_2026.02.09_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2026.02.09/i915-sriov-dkms_2026.02.09_amd64.deb"
sudo dpkg -i /tmp/i915-sriov-dkms_2026.02.09_amd64.deb
```

### 3. Update kernel parameters in cloud-init GRUB configs
Cloud-init configurations override `/etc/default/grub` with their own settings in `/etc/default/grub.d/50-cloudimg-settings.cfg`. You need to edit that file instead of the default grub configuration:
```bash
sudo nano /etc/default/grub.d/50-cloudimg-settings.cfg
```
Change `GRUB_CMDLINE_LINUX_DEFAULT` to:
```bash
GRUB_CMDLINE_LINUX_DEFAULT="console=tty1 console=ttyS0 i915.enable_guc=3 module_blacklist=xe"
```

### 4. Update grub and initramfs, then reboot
```bash
sudo update-grub
sudo update-initramfs -u
sudo reboot
```

### 5. Verify VF recognition
After rebooting, verify if the Virtual Function (VF) is recognized:
```bash
sudo dmesg | grep i915
```
You should see output similar to this:
```
i915: You are using the i915-sriov-dkms module, a ported version of the i915/xe module with SR-IOV support.
```

*(Tested with `Intel Core i5-13500T` Alder Lake)*

## Uninstallation

Remove the package with `sudo dpkg -P i915-sriov-dkms`.

If you installed the module manually, or if the package manager fails to remove it from the kernel tree, you can remove it forcibly with:
`sudo dkms remove i915-sriov-dkms/2026.02.09`
