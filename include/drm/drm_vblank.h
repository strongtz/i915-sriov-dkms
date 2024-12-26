#include <linux/version.h>
#include_next <drm/drm_vblank.h>

#ifndef _BACKPORT_DRM_VBLANK_H
#define _BACKPORT_DRM_VBLANK_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
struct drm_vblank_crtc *drm_crtc_vblank_crtc(struct drm_crtc *crtc);
#endif

#endif