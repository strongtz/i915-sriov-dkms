# NixOS Linux Host Installation Steps (Tested Kernel 6.17)

For NixOS users, the i915-sriov kernel module can be directly included in your NixOS configuration without the use of DKMS. In particular, the kernel module is provided as a NixOS module that must be included in your NixOS configuration. This NixOS module places the i915-sriov kernel module via an overlay in your `pkgs` attribute set with the attribute name `i915-sriov`. This kernel module can then be included in your configuration by declaring `boot.extraModulePackages = [ pkgs.i915-sriov ];` The same applies also to `xe-sriov`. It is recommended to set `inputs.nixpkgs.follows = "nixpkgs"` to avoid version mismatch.

## Optional Configurations

### Block VFs on the Host
Leaving VFs exposed to the host can lead to system instability and software conflicts. VFs provide no functional benefit to the host environment; their sole purpose is to serve guest virtual machines. To ensure host stability and performance, it is highly recommended to block these VFs from the host operating system.

To apply this configuration, follow the **[Block VFs Setup Guide](block-vfs.md)**.
