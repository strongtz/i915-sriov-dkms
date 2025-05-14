/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FBDEV_H__
#define __INTEL_FBDEV_H__
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
struct drm_device;
#else
struct drm_fb_helper;
struct drm_fb_helper_surface_size;
#endif

struct drm_i915_private;
struct intel_fbdev;
struct intel_framebuffer;

#ifdef CONFIG_DRM_FBDEV_EMULATION
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
int intel_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes);
#define INTEL_FBDEV_DRIVER_OPS \
	.fbdev_probe = intel_fbdev_driver_fbdev_probe
#endif
void intel_fbdev_setup(struct drm_i915_private *dev_priv);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous);
#endif
struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev);
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
#define INTEL_FBDEV_DRIVER_OPS \
	.fbdev_probe = NULL
#endif

static inline void intel_fbdev_setup(struct drm_i915_private *dev_priv)
{
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
static inline void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous)
{
}
#endif

static inline struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev)
{
	return NULL;
}
#endif

#endif /* __INTEL_FBDEV_H__ */
