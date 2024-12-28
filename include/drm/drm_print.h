#include <linux/version.h>
#include_next <drm/drm_print.h>

#ifndef _BACKPORT_DRM_DRM_PRINT_H
#define _BACKPORT_DRM_DRM_PRINT_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
#define drm_dbg_ratelimited(drm, fmt, ...) \
	__DRM_DEFINE_DBG_RATELIMITED(DRIVER, drm, fmt, ## __VA_ARGS__)
#endif

#endif