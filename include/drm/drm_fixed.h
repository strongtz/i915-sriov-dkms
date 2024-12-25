#include <linux/version.h>
#include_next <drm/drm_fixed.h>

#ifndef _BACKPORT_DRM_FIXED_H
#define _BACKPORT_DRM_FIXED_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
static inline int fxp_q4_from_int(int val_int)
{
	return val_int << 4;
}

static inline int fxp_q4_to_int(int val_q4)
{
	return val_q4 >> 4;
}

static inline int fxp_q4_to_int_roundup(int val_q4)
{
	return (val_q4 + 0xf) >> 4;
}

static inline int fxp_q4_to_frac(int val_q4)
{
	return val_q4 & 0xf;
}

#define FXP_Q4_FMT		"%d.%04d"
#define FXP_Q4_ARGS(val_q4)	fxp_q4_to_int(val_q4), (fxp_q4_to_frac(val_q4) * 625)
#endif

#endif