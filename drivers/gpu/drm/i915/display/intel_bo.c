// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_panic.h>
#include "display/intel_display_types.h"
#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_object.h"
#include "gem/i915_gem_object_frontbuffer.h"
#include "i915_debugfs.h"
#include "intel_bo.h"

bool intel_bo_is_tiled(struct drm_gem_object *obj)
{
	return i915_gem_object_is_tiled(to_intel_bo(obj));
}

bool intel_bo_is_userptr(struct drm_gem_object *obj)
{
	return i915_gem_object_is_userptr(to_intel_bo(obj));
}

bool intel_bo_is_shmem(struct drm_gem_object *obj)
{
	return i915_gem_object_is_shmem(to_intel_bo(obj));
}

bool intel_bo_is_protected(struct drm_gem_object *obj)
{
	return i915_gem_object_is_protected(to_intel_bo(obj));
}

void intel_bo_flush_if_display(struct drm_gem_object *obj)
{
	i915_gem_object_flush_if_display(to_intel_bo(obj));
}

int intel_bo_fb_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	return i915_gem_fb_mmap(to_intel_bo(obj), vma);
}

int intel_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size)
{
	return i915_gem_object_read_from_page(to_intel_bo(obj), offset, dst, size);
}

struct intel_frontbuffer *intel_bo_get_frontbuffer(struct drm_gem_object *obj)
{
	return i915_gem_object_get_frontbuffer(to_intel_bo(obj));
}

struct intel_frontbuffer *intel_bo_set_frontbuffer(struct drm_gem_object *obj,
						   struct intel_frontbuffer *front)
{
	return i915_gem_object_set_frontbuffer(to_intel_bo(obj), front);
}

void intel_bo_describe(struct seq_file *m, struct drm_gem_object *obj)
{
	i915_debugfs_describe_obj(m, to_intel_bo(obj));
}

struct intel_framebuffer *intel_bo_alloc_framebuffer(void)
{
	return i915_gem_object_alloc_framebuffer();
}

int intel_bo_panic_setup(struct drm_scanout_buffer *sb)
{
	return i915_gem_object_panic_setup(sb);
}

void intel_bo_panic_finish(struct intel_framebuffer *fb)
{
	return i915_gem_object_panic_finish(fb);
}
