#include_next <drm/intel/pciids.h>
#ifndef __BACKPORT_DRM_INTEL_PCIIDS_H__
#define __BACKPORT_DRM_INTEL_PCIIDS_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,14,0)
/* DG2 */
#define INTEL_DG2_G10_D_IDS(MACRO__, ...) \
	MACRO__(0x56A0, ## __VA_ARGS__), \
	MACRO__(0x56A1, ## __VA_ARGS__), \
	MACRO__(0x56A2, ## __VA_ARGS__)

#define INTEL_DG2_G10_E_IDS(MACRO__, ...) \
	MACRO__(0x56BE, ## __VA_ARGS__), \
	MACRO__(0x56BF, ## __VA_ARGS__)

#define INTEL_DG2_G10_M_IDS(MACRO__, ...) \
	MACRO__(0x5690, ## __VA_ARGS__), \
	MACRO__(0x5691, ## __VA_ARGS__), \
	MACRO__(0x5692, ## __VA_ARGS__)

#ifdef INTEL_DG2_G10_IDS
#undef INTEL_DG2_G10_IDS
#endif

#define INTEL_DG2_G10_IDS(MACRO__, ...) \
	INTEL_DG2_G10_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G10_E_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G10_M_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_DG2_G11_D_IDS(MACRO__, ...) \
	MACRO__(0x56A5, ## __VA_ARGS__), \
	MACRO__(0x56A6, ## __VA_ARGS__), \
	MACRO__(0x56B0, ## __VA_ARGS__), \
	MACRO__(0x56B1, ## __VA_ARGS__)

#define INTEL_DG2_G11_E_IDS(MACRO__, ...) \
	MACRO__(0x56BA, ## __VA_ARGS__), \
	MACRO__(0x56BB, ## __VA_ARGS__), \
	MACRO__(0x56BC, ## __VA_ARGS__), \
	MACRO__(0x56BD, ## __VA_ARGS__)

#define INTEL_DG2_G11_M_IDS(MACRO__, ...) \
	MACRO__(0x5693, ## __VA_ARGS__), \
	MACRO__(0x5694, ## __VA_ARGS__), \
	MACRO__(0x5695, ## __VA_ARGS__)

#ifdef INTEL_DG2_G11_IDS
#undef INTEL_DG2_G11_IDS
#endif

#define INTEL_DG2_G11_IDS(MACRO__, ...) \
	INTEL_DG2_G11_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G11_E_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G11_M_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_DG2_G12_D_IDS(MACRO__, ...) \
	MACRO__(0x56A3, ## __VA_ARGS__), \
	MACRO__(0x56A4, ## __VA_ARGS__), \
	MACRO__(0x56B2, ## __VA_ARGS__), \
	MACRO__(0x56B3, ## __VA_ARGS__)

#define INTEL_DG2_G12_M_IDS(MACRO__, ...) \
	MACRO__(0x5696, ## __VA_ARGS__), \
	MACRO__(0x5697, ## __VA_ARGS__)

#ifdef INTEL_DG2_G12_IDS
#undef INTEL_DG2_G12_IDS
#endif

#define INTEL_DG2_G12_IDS(MACRO__, ...) \
	INTEL_DG2_G12_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G12_M_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_DG2_D_IDS(MACRO__, ...) \
	INTEL_DG2_G10_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G11_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G12_D_IDS(MACRO__, ## __VA_ARGS__)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,15,0)
/* MTL */
#define INTEL_MTL_U_IDS(MACRO__, ...) \
	MACRO__(0x7D40, ## __VA_ARGS__), \
	MACRO__(0x7D45, ## __VA_ARGS__)
#endif

#endif
