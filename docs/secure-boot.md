# UEFI Secure Boot Enabled Configuration (Optional)

Note: Only applicable to Ubuntu, PVE, or other distributions based on Debian. If secure boot support is required for you, please enable UEFI secure boot before installing i915-sriov-dkms. For PVE, it is important to ensure that secure boot is enabled when installing PVE, otherwise in some ZFS based installations, a kernel that is not signed may be installed by default, which cannot support secure boot. In this situation, it is necessary to first refer to the PVE documentation to configure secure boot. Arch Linux users please refer to the [Arch Linux Wiki](https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#shim).

The precondition for executing the following steps: `UEFI Secure Boot is turned on and in User/Standard Mode.`

1. Install `mokutil`.
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
