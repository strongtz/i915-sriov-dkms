LINUXINCLUDE := \
	-I$(src)/include \
	-I$(src)/include/trace \
	$(LINUXINCLUDE)

subdir-ccflags-y += \
	-DDKMS_MODULE_VERSION="\"2025.09.03-sriov\"" \
	-DDKMS_MODULE_ORIGIN_KERNEL="\"6.17-rc4\""

obj-m += drivers/gpu/drm/i915/
obj-m += drivers/gpu/drm/xe/

.PHONY: default clean modules load unload install patch
