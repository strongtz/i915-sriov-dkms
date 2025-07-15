{
  description = "kernel module of Linux i915 driver with SR-IOV support";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
  let
    # dummy NixOS configuration with the i915-sriov driver
    testNixosConfiguration = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [(
        { pkgs, modulesPath, ... }: {
          imports = [
            "${modulesPath}/profiles/minimal.nix" # reduce NixOS system size
            self.nixosModules.default # include i915-sriov kernel module in pkgs
          ];

          # reduce NixOS system size
          nix.enable = false;
          system.switch.enable = false;

          # required for dummy system
          fileSystems."/" = { device = "/dev/sda1"; fsType = "ext4"; };
          boot.loader.systemd-boot.enable = true;
          system.stateVersion = "25.11"; # do not change, see https://nixos.org/manual/nixos/stable/options.html#opt-system.stateVersion

          # build and use i915-sriov kernel module
          boot.extraModulePackages = [ pkgs.i915-sriov ];
        }
      )];
    };
  in {
    # builds the i915-sriov driver
    # this requires a dummy NixOS configuration due to the driver build environment depending on the specific kernel that is used
    checks.x86_64-linux.build-i915-sriov = testNixosConfiguration.config.system.build.toplevel;

    # include this NixOS module to include the i915-sriov kernel module as a derivation in your pkgs via an overlay
    nixosModules.default = { config, ... }: {
      nixpkgs.overlays = [(final: prev: {
        i915-sriov = config.boot.kernelPackages.callPackage ./default.nix { };
      })];
    };
  };
}
