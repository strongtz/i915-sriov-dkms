{
  lib,
  stdenv,
  nix-gitignore,

  kernel
}:
let
  pkgbuildLines = lib.strings.splitString "\n" (builtins.readFile ./PKGBUILD);
  versionDefinition = lib.lists.findFirst (x: lib.strings.hasPrefix "pkgver=" x) "unknown" pkgbuildLines;
  version = lib.strings.removePrefix "pkgver=" versionDefinition;
in
stdenv.mkDerivation rec {
  inherit version;
  name = "i915-sriov-module-${version}-${kernel.version}";

  src = lib.cleanSource (nix-gitignore.gitignoreSourcePure [
      ./.gitignore
      "result*"
    ] ./.
  );

  hardeningDisable = [ "pic" "format" ];

  nativeBuildInputs = kernel.moduleBuildDependencies;

  buildPhase = ''
    make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M=$PWD
  '';

  installPhase = ''
    install -D i915.ko $out/lib/modules/${kernel.modDirVersion}/kernel/drivers/gpu/drm/i915/i915.ko
  '';

  meta = {
    platforms = [ "x86_64-linux" ];
    description = "i915 patched with SR-IOV vGPU functionality";
    homepage = "https://github.com/strongtz/i915-sriov-dkms";
  };
}
