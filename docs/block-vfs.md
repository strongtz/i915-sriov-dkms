# Block VFs on the Host (Optional)

While enabling VFs, leaving VFs exposed to the host can lead to system instability and software conflicts. In certain desktop environments, hot-plugging or unbinding VFs while assigning them to KVM virtual machines can trigger a complete GUI session crash (See [issue#279](https://github.com/strongtz/i915-sriov-dkms/issues/279)). Many host-side applications that utilize the iGPU (e.g., Media Servers like Emby/Plex or monitoring tools like intel_gpu_top) may incorrectly identify and attempt to use the VFs. This often results in abnormal output, failed hardware transcoding, or initialization errors. VFs provide no functional benefit to the host environment; their sole purpose is to serve guest virtual machines.

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
