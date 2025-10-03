/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_fb_helper.h>

#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fbdev_fb.h"
#include "xe_bo.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_wa.h"

#include <generated/xe_wa_oob.h>

struct intel_framebuffer *intel_fbdev_fb_alloc(struct drm_fb_helper *helper,
					       struct drm_fb_helper_surface_size *sizes)
{
	struct drm_framebuffer *fb;
	struct drm_device *dev = helper->dev;
	struct xe_device *xe = to_xe_device(dev);
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct xe_bo *obj;
	int size;

	/* we don't do packed 24bpp */
	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width *
				    DIV_ROUND_UP(sizes->surface_bpp, 8), XE_PAGE_SIZE);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = PAGE_ALIGN(size);
	obj = ERR_PTR(-ENODEV);

	if (!IS_DGFX(xe) && !XE_WA(xe_root_mmio_gt(xe), 22019338487_display)) {
		obj = xe_bo_create_pin_map(xe, xe_device_get_root_tile(xe),
					   NULL, size,
					   ttm_bo_type_kernel, XE_BO_FLAG_SCANOUT |
					   XE_BO_FLAG_STOLEN |
					   XE_BO_FLAG_GGTT);
		if (!IS_ERR(obj))
			drm_info(&xe->drm, "Allocated fbdev into stolen\n");
		else
			drm_info(&xe->drm, "Allocated fbdev into stolen failed: %li\n", PTR_ERR(obj));
	}

	if (IS_ERR(obj)) {
		obj = xe_bo_create_pin_map(xe, xe_device_get_root_tile(xe), NULL, size,
					   ttm_bo_type_kernel, XE_BO_FLAG_SCANOUT |
					   XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(xe)) |
					   XE_BO_FLAG_GGTT);
	}

	if (IS_ERR(obj)) {
		drm_err(&xe->drm, "failed to allocate framebuffer (%pe)\n", obj);
		fb = ERR_PTR(-ENOMEM);
		goto err;
	}

	fb = intel_framebuffer_create(&obj->ttm.base,
				      drm_get_format_info(dev,
							  mode_cmd.pixel_format,
							  mode_cmd.modifier[0]),
				      &mode_cmd);
	if (IS_ERR(fb)) {
		xe_bo_unpin_map_no_vm(obj);
		goto err;
	}

	drm_gem_object_put(&obj->ttm.base);

	return to_intel_framebuffer(fb);

err:
	return ERR_CAST(fb);
}

int intel_fbdev_fb_fill_info(struct intel_display *display, struct fb_info *info,
			     struct drm_gem_object *_obj, struct i915_vma *vma)
{
	struct xe_bo *obj = gem_to_xe_bo(_obj);
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);

	if (!(obj->flags & XE_BO_FLAG_SYSTEM)) {
		if (obj->flags & XE_BO_FLAG_STOLEN)
			info->fix.smem_start = xe_ttm_stolen_io_offset(obj, 0);
		else
			info->fix.smem_start =
				pci_resource_start(pdev, 2) +
				xe_bo_addr(obj, 0, XE_PAGE_SIZE);

		info->fix.smem_len = obj->ttm.base.size;
	} else {
		/* XXX: Pure fiction, as the BO may not be physically accessible.. */
		info->fix.smem_start = 0;
		info->fix.smem_len = obj->ttm.base.size;
	}
	XE_WARN_ON(iosys_map_is_null(&obj->vmap));

	info->screen_base = obj->vmap.vaddr_iomem;
	info->screen_size = obj->ttm.base.size;

	return 0;
}
