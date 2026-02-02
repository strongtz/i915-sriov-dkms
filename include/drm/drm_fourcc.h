#include_next <drm/drm_fourcc.h>

#ifndef __BACKPORT_DRM_FOURCC_H__
#define __BACKPORT_DRM_FOURCC_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
const struct drm_format_info *
backport__drm_get_format_info6p16(struct drm_device *dev,
		    u32 pixel_format, u64 modifier);
#endif

#endif /* __BACKPORT_DRM_FOURCC_H__ */
