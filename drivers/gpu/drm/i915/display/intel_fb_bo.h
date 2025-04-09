/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#include <linux/version.h>

#ifndef __INTEL_FB_BO_H__
#define __INTEL_FB_BO_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
struct drm_file;
struct drm_mode_fb_cmd2;
struct drm_i915_gem_object;
struct drm_i915_private;
struct intel_framebuffer;
#else
struct drm_file;
struct drm_gem_object;
struct drm_i915_private;
struct drm_mode_fb_cmd2;
struct intel_framebuffer;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
void intel_fb_bo_framebuffer_fini(struct drm_i915_gem_object *obj);
#else
void intel_fb_bo_framebuffer_fini(struct drm_gem_object *obj);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
int intel_fb_bo_framebuffer_init(struct intel_framebuffer *intel_fb,
				 struct drm_i915_gem_object *obj,
				 struct drm_mode_fb_cmd2 *mode_cmd);
#else
int intel_fb_bo_framebuffer_init(struct intel_framebuffer *intel_fb,
				 struct drm_gem_object *obj,
				 struct drm_mode_fb_cmd2 *mode_cmd);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
struct drm_i915_gem_object *
intel_fb_bo_lookup_valid_bo(struct drm_i915_private *i915,
			    struct drm_file *filp,
			    const struct drm_mode_fb_cmd2 *user_mode_cmd);
#else
struct drm_gem_object *
intel_fb_bo_lookup_valid_bo(struct drm_i915_private *i915,
			    struct drm_file *filp,
			    const struct drm_mode_fb_cmd2 *user_mode_cmd);
#endif

#endif
