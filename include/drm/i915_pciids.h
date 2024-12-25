#include <linux/version.h>
#include_next <drm/i915_pciids.h>
#ifndef _BACKPORT_DRM_I915_PCIIDS_H
#define _BACKPORT_DRM_I915_PCIIDS_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
#define INTEL_PNV_G_IDS(info) \
	INTEL_PINEVIEW_G_IDS(info)

#define INTEL_PNV_M_IDS(info) \
	INTEL_PINEVIEW_M_IDS(info)

#define INTEL_PNV_IDS(info) \
	INTEL_PNV_G_IDS(info), \
	INTEL_PNV_M_IDS(info)

#define INTEL_ILK_D_IDS(info) \
	INTEL_IRONLAKE_D_IDS(info)

#define INTEL_ILK_M_IDS(info) \
	INTEL_IRONLAKE_M_IDS(info)

#define INTEL_ILK_IDS(info) \
	INTEL_ILK_D_IDS(info), \
	INTEL_ILK_M_IDS(info)

#define INTEL_SNB_IDS(info) \
	INTEL_SNB_D_IDS(info), \
	INTEL_SNB_M_IDS(info)

#define INTEL_IVB_IDS(info) \
	INTEL_IVB_M_IDS(info), \
	INTEL_IVB_D_IDS(info)

#define INTEL_CML_IDS(info) \
	INTEL_CML_GT1_IDS(info), \
	INTEL_CML_GT2_IDS(info), \
	INTEL_CML_U_GT1_IDS(info), \
	INTEL_CML_U_GT2_IDS(info)

#undef INTEL_CFL_IDS
#define INTEL_CFL_IDS(info)	   \
	INTEL_CFL_S_GT1_IDS(info), \
	INTEL_CFL_S_GT2_IDS(info), \
	INTEL_CFL_H_GT1_IDS(info), \
	INTEL_CFL_H_GT2_IDS(info), \
	INTEL_CFL_U_GT2_IDS(info), \
	INTEL_CFL_U_GT3_IDS(info), \
	INTEL_AML_CFL_GT2_IDS(info)

#define INTEL_WHL_IDS(info) \
	INTEL_WHL_U_GT1_IDS(info), \
	INTEL_WHL_U_GT2_IDS(info), \
	INTEL_WHL_U_GT3_IDS(info)

#define INTEL_ICL_IDS(info) \
	INTEL_ICL_11_IDS(info)

#define INTEL_TGL_GT1_IDS(info) \
	INTEL_TGL_12_GT1_IDS(info)

#define INTEL_TGL_GT2_IDS(info) \
	INTEL_TGL_12_GT2_IDS(info)

#define INTEL_TGL_IDS(info) \
	INTEL_TGL_GT1_IDS(info), \
	INTEL_TGL_GT2_IDS(info)

#undef INTEL_RPLP_IDS
#define INTEL_RPLP_IDS(info) \
	INTEL_VGA_DEVICE(0xA720, info), \
	INTEL_VGA_DEVICE(0xA7A0, info), \
	INTEL_VGA_DEVICE(0xA7A8, info), \
	INTEL_VGA_DEVICE(0xA7AA, info), \
	INTEL_VGA_DEVICE(0xA7AB, info)

/* LNL */
#define INTEL_LNL_IDS(info) \
	INTEL_VGA_DEVICE(0x6420, info), \
	INTEL_VGA_DEVICE(0x64A0, info), \
	INTEL_VGA_DEVICE(0x64B0, info)

/* BMG */
#define INTEL_BMG_IDS(info) \
	INTEL_VGA_DEVICE(0xE202, info), \
	INTEL_VGA_DEVICE(0xE20B, info), \
	INTEL_VGA_DEVICE(0xE20C, info), \
	INTEL_VGA_DEVICE(0xE20D, info), \
	INTEL_VGA_DEVICE(0xE212, info)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
/* ARL */
#define INTEL_ARL_H_IDS(info) \
	INTEL_VGA_DEVICE(0x7D51, info), \
	INTEL_VGA_DEVICE(0x7DD1, info)

#define INTEL_ARL_U_IDS(info) \
	INTEL_VGA_DEVICE(0x7D41, info) \

#define INTEL_ARL_S_IDS(info) \
	INTEL_VGA_DEVICE(0x7D67, info), \
	INTEL_VGA_DEVICE(0xB640, info)
#endif

#endif