# Linux i915 driver with SR-IOV support (dkms module)

Originally from [linux-intel-lts](https://github.com/intel/linux-intel-lts/tree/lts-v5.15.49-adl-linux-220826T092047Z/drivers/gpu/drm/i915)

## Notice

This package is **highly experimental**, you should only use it when you know what you are doing.

## My testing cmdline

```
intel_iommu=on i915.enable_guc=7
```

## Creating virtual functions

```
echo 2 > /sys/devices/pci0000:00/0000:00:02.0/sriov_numvfs
```

You can create up to 7 VFs on UHD Graphics 770
