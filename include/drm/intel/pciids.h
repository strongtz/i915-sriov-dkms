#include_next <drm/intel/pciids.h>
#ifndef __BACKPORT_DRM_INTEL_PCIIDS_H__
#define __BACKPORT_DRM_INTEL_PCIIDS_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,15,0)
/* MTL */
#define INTEL_MTL_U_IDS(MACRO__, ...) \
	MACRO__(0x7D40, ## __VA_ARGS__), \
	MACRO__(0x7D45, ## __VA_ARGS__)
#endif

#endif
