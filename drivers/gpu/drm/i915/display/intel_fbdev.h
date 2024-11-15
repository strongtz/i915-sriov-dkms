/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FBDEV_H__
#define __INTEL_FBDEV_H__

#include <linux/types.h>
#include <linux/version.h>

struct drm_device;
struct drm_i915_private;
struct intel_fbdev;
struct intel_framebuffer;

#ifdef CONFIG_DRM_FBDEV_EMULATION
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
int intel_fbdev_init(struct drm_device *dev);
void intel_fbdev_initial_config_async(struct drm_i915_private *dev_priv);
void intel_fbdev_unregister(struct drm_i915_private *dev_priv);
void intel_fbdev_fini(struct drm_i915_private *dev_priv);
#else
void intel_fbdev_setup(struct drm_i915_private *dev_priv);
#endif
void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
void intel_fbdev_output_poll_changed(struct drm_device *dev);
void intel_fbdev_restore_mode(struct drm_i915_private *dev_priv);
#endif
struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
static inline int intel_fbdev_init(struct drm_device *dev)
{
	return 0;
}

static inline void intel_fbdev_initial_config_async(struct drm_i915_private *dev_priv)
{
}

static inline void intel_fbdev_unregister(struct drm_i915_private *dev_priv)
{
}

static inline void intel_fbdev_fini(struct drm_i915_private *dev_priv)
{
}
#else
static inline void intel_fbdev_setup(struct drm_i915_private *dev_priv)
{
}
#endif
static inline void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous)
{
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
static inline void intel_fbdev_output_poll_changed(struct drm_device *dev)
{
}

static inline void intel_fbdev_restore_mode(struct drm_i915_private *i915)
{
}
#endif

static inline struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev)
{
	return NULL;
}
#endif

#endif /* __INTEL_FBDEV_H__ */
