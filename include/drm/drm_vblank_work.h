#include <linux/version.h>
#include_next <drm/drm_vblank_work.h>

#ifndef _BACKPORT_DRM_VBLANK_WORK_H
#define _BACKPORT_DRM_VBLANK_WORK_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
void drm_vblank_work_flush_all(struct drm_crtc *crtc);
#endif
#endif