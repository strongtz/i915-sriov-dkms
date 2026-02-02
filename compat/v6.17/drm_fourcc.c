/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>

#include <drm/drm_fourcc.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
/**
 * drm_get_format_info - query information for a given framebuffer configuration
 * @dev: DRM device
 * @pixel_format: pixel format (DRM_FORMAT_*)
 * @modifier: modifier
 *
 * Returns:
 * The instance of struct drm_format_info that describes the pixel format, or
 * NULL if the format is unsupported.
 */
const struct drm_format_info *
backport__drm_get_format_info6p16(struct drm_device *dev,
		    u32 pixel_format, u64 modifier)
{
	struct drm_mode_fb_cmd2 cmd = {0};
	
	cmd.pixel_format = pixel_format;
	cmd.modifier[0] = modifier;
	cmd.flags |= DRM_MODE_FB_MODIFIERS;

	return drm_get_format_info(dev, &cmd);
}
EXPORT_SYMBOL(backport__drm_get_format_info6p16);
#endif
