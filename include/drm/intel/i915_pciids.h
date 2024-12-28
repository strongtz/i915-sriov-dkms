#include <linux/version.h>
#include_next <drm/intel/i915_pciids.h>
#ifndef _BACKPORT_DRM_INTEL_I915_PCIIDS_H
#define _BACKPORT_DRM_INTEL_I915_PCIIDS_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
/* ARL */
#define INTEL_ARL_H_IDS(MACRO__, ...) \
	MACRO__(0x7D51, ## __VA_ARGS__), \
	MACRO__(0x7DD1, ## __VA_ARGS__)

#define INTEL_ARL_U_IDS(MACRO__, ...) \
	MACRO__(0x7D41, ## __VA_ARGS__) \

#define INTEL_ARL_S_IDS(MACRO__, ...) \
	MACRO__(0x7D67, ## __VA_ARGS__), \
	MACRO__(0xB640, ## __VA_ARGS__)
#endif

#endif