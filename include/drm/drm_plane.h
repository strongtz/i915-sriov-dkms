#include <linux/version.h>
#include_next <drm/drm_plane.h>

#ifndef _BACKPORT_DRM_DRM_PLANE_H
#define _BACKPORT_DRM_DRM_PLANE_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
bool drm_plane_has_format(struct drm_plane *plane,
			  u32 format, u64 modifier);
#endif

#endif