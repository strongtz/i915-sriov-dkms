DKMS_MODULE_VERSION := "2025.10.10-sriov"
DKMS_MODULE_ORIGIN_KERNEL := "6.17"

LINUXINCLUDE := \
	-I$(src)/include \
	-I$(src)/include/trace \
	$(LINUXINCLUDE) \
	-include $(src)/include/config.h

subdir-ccflags-y += \
	-DDKMS_MODULE_VERSION='$(DKMS_MODULE_VERSION)' \
	-DDKMS_MODULE_ORIGIN_KERNEL='$(DKMS_MODULE_ORIGIN_KERNEL)' \
	-DDKMS_MODULE_SOURCE_DIR='$(abspath $(src))'

obj-m += compat/
obj-m += drivers/gpu/drm/i915/
obj-m += drivers/gpu/drm/xe/

.PHONY: default clean modules load unload install patch
