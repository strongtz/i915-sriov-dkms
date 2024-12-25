#include <linux/version.h>
#include_next <drm/display/drm_dsc_helper.h>

#ifndef _BACKPORT_DRM_DISPLAY_DRM_DSC_HELPER_H
#define _BACKPORT_DRM_DISPLAY_DRM_DSC_HELPER_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
struct drm_printer;
void drm_dsc_dump_config(struct drm_printer *p, int indent, const struct drm_dsc_config *cfg);
#endif

#endif