/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_FBDEV_FB_H__
#define __INTEL_FBDEV_FB_H__

struct drm_fb_helper;
struct drm_fb_helper_surface_size;
struct drm_gem_object;
struct fb_info;
struct i915_vma;
struct intel_display;

struct intel_framebuffer *intel_fbdev_fb_alloc(struct drm_fb_helper *helper,
					       struct drm_fb_helper_surface_size *sizes);
int intel_fbdev_fb_fill_info(struct intel_display *display, struct fb_info *info,
			     struct drm_gem_object *obj, struct i915_vma *vma);

#endif
