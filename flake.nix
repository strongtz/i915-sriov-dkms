{
  description = "kernel module of Linux i915 driver with SR-IOV support";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      lib = nixpkgs.lib;

      pkgbuildLines = lib.strings.splitString "\n" (builtins.readFile ./PKGBUILD);
      versionDefinition = lib.lists.findFirst (
        line: lib.strings.hasPrefix "pkgver=" line
      ) "unknown" pkgbuildLines;
      version = lib.strings.removePrefix "pkgver=" versionDefinition;

      i915Module =
        {
          stdenv,
          nix-gitignore,
          kernel,
        }:
        stdenv.mkDerivation rec {
          inherit version;
          name = "i915-sriov-module-${version}-${kernel.version}";

          src = lib.cleanSource (
            nix-gitignore.gitignoreSourcePure [
              ./.gitignore
              "result*"
            ] ./.
          );

          hardeningDisable = [
            "pic"
            "format"
          ];
          nativeBuildInputs = kernel.moduleBuildDependencies;

          buildPhase = ''
            make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build \
              M=$PWD \
              drivers/gpu/drm/i915/i915.ko \
              compat/intel_sriov_compat.ko
          '';

          installPhase = ''
            install -D drivers/gpu/drm/i915/i915.ko \
              $out/lib/modules/${kernel.modDirVersion}/updates/drivers/gpu/drm/i915/i915.ko
            install -D compat/intel_sriov_compat.ko \
              $out/lib/modules/${kernel.modDirVersion}/updates/compat/gpu/drm/i915/intel_sriov_compat.ko
          '';

          meta = {
            platforms = [ "x86_64-linux" ];
            description = "Intel i915 driver patched with SR-IOV vGPU functionality";
            homepage = "https://github.com/strongtz/i915-sriov-dkms";
          };
        };

      xeModule =
        {
          stdenv,
          nix-gitignore,
          kernel,
        }:
        stdenv.mkDerivation rec {
          inherit version;
          name = "xe-sriov-module-${version}-${kernel.version}";

          src = lib.cleanSource (
            nix-gitignore.gitignoreSourcePure [
              ./.gitignore
              "result*"
            ] ./.
          );

          hardeningDisable = [
            "pic"
            "format"
          ];
          nativeBuildInputs = kernel.moduleBuildDependencies;

          buildPhase = ''
            make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build \
              M=$PWD \
              compat/intel_sriov_compat.ko \
              drivers/gpu/drm/xe/xe.ko
          '';

          installPhase = ''
            install -D drivers/gpu/drm/xe/xe.ko \
              $out/lib/modules/${kernel.modDirVersion}/updates/drivers/gpu/drm/xe/xe.ko
            install -D compat/intel_sriov_compat.ko \
              $out/lib/modules/${kernel.modDirVersion}/updates/compat/gpu/drm/xe/intel_sriov_compat.ko
          '';

          meta = {
            platforms = [ "x86_64-linux" ];
            description = "Intel xe driver patched with SR-IOV vGPU functionality";
            homepage = "https://github.com/strongtz/i915-sriov-dkms";
          };
        };

      # dummy NixOS configuration with the SR-IOV drivers
      testNixosConfiguration = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [
          (
            { pkgs, modulesPath, ... }:
            {
              imports = [
                "${modulesPath}/profiles/minimal.nix" # reduce NixOS system size
                self.nixosModules.default # include the SR-IOV kernel modules in pkgs
              ];

              # reduce NixOS system size
              nix.enable = false;
              system.switch.enable = false;

              # required for dummy system
              fileSystems."/" = {
                device = "/dev/sda1";
                fsType = "ext4";
              };
              boot = {
                loader.systemd-boot.enable = true;
                kernelPackages = pkgs.linuxPackages_latest;
              };

              system.stateVersion = "25.11"; # do not change, see https://nixos.org/manual/nixos/stable/options.html#opt-system.stateVersion

              # build and use both SR-IOV kernel modules
              boot.extraModulePackages = [
                pkgs.i915-sriov
                pkgs.xe-sriov
              ];
            }
          )
        ];
      };
    in
    {
      # builds the SR-IOV drivers
      # this requires a dummy NixOS configuration due to the driver build environment depending on the specific kernel that is used
      checks.x86_64-linux.build-i915-sriov = testNixosConfiguration.config.system.build.toplevel;

      # include this NixOS module to include the SR-IOV kernel modules as derivations in your pkgs via an overlay
      nixosModules.default =
        { config, ... }:
        {
          nixpkgs.overlays = [
            (final: prev: {
              i915-sriov = config.boot.kernelPackages.callPackage i915Module { };
              xe-sriov = config.boot.kernelPackages.callPackage xeModule { };
            })
          ];
        };
    };
}
