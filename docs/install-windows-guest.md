# Windows Guest (Tested with Proxmox 8.3 + Windows 11 24H2 + Intel Driver 32.0.101.6460/32.0.101.6259)

Thanks for [resiliencer](https://github.com/resiliencer) and his contribution in [#225](https://github.com/strongtz/i915-sriov-dkms/issues/225#issue-2687672590).

These steps ensure compatibility across all driver versions. In theory you can install any version and won't be hit by the dreaded `Code 43`.

### Extract Graphics EFI Firmware

1. Download [UEFITools](https://github.com/LongSoft/UEFITool/releases) (`UEFITool_NE_A68_win64` for Windows. They supply Linux and Mac binaries, too)
2. Download BIOS for motherboard (I suspect any motherboard BIOS would work as long as it is for Alder/Raptor Lake Desktop Platform)
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
4. When the VM powered off, edit the configuration file:

```
# Passing the 02.1 VF, specify the romfile. ROM path is relative

hostpci0: 0000:00:02.1,pcie=1,romfile=Intelgopdriver_desktop.efi,x-vga=1
```

5. In the `Hardware` tab, set `Display` to `none`.
6. Start the VM. You won't be able to access it with console, so your only way in is Remote Desktop. Once you are in, download the graphics driver from Intel, any version should work.
7. During install, you may experience black screen when the actual graphics drivers are being installed and applied. This black screen will persist until you restart the VM. My advice is give it a few minutes to do its thing. You can make your best judgement by looking at the VM CPU usage in Proxmox.
8. After rebooting, connect with RDP once again. Go to Device Manager and verify the result. You should see the Intel Graphics is installed and working.

![CleanShot 2025-01-27 at 12 26 28](https://github.com/user-attachments/assets/7e48877f-2b57-42ac-bd0b-c1aa72bddc40)
