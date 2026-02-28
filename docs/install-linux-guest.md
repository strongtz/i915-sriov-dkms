# Linux Guest Installation Steps (Ubuntu 25.04/Kernel 6.14)

We will need to run the same driver under Linux guests.

1. Install build tools
   ```
   apt install build-essential dkms linux-headers-$(uname -r)
   ```
2. Download and install the `.deb`
   ```
   wget -O /tmp/i915-sriov-dkms_2026.02.09_amd64.deb "https://github.com/strongtz/i915-sriov-dkms/releases/download/2026.02.09/i915-sriov-dkms_2026.02.09_amd64.deb"
   dpkg -i /tmp/i915-sriov-dkms_2026.02.09_amd64.deb
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

## Uninstallation

Remove the package with `dpkg -P i915-sriov-dkms`.

If you installed the module manually, or if the package manager fails to remove it from the kernel tree, you can remove it forcibly with:
`dkms remove i915-sriov-dkms/2026.02.09`
