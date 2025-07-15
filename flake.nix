{
  description = "kernel module of Linux i915 driver with SR-IOV support";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }: {
    # include this NixOS module to include the i915-sriov kernel module as a derivation in your pkgs via an overlay
    nixosModules.default = { config, ... }: {
      nixpkgs.overlays = [(final: prev: {
        i915-sriov = config.boot.kernelPackages.callPackage ./default.nix { };
      })];
    };
  };
}
