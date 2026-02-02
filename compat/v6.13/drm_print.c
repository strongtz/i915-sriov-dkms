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

#include <drm/drm_print.h>
#include <drm/drm_device.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
void __drm_printfn_line_dummy(struct drm_printer *p, struct va_format *vaf)
{
	const char *prefix = p->prefix ?: "";
	const char *pad = p->prefix ? " " : "";

	drm_printf(p->arg, "%s%s: %pV", prefix, pad, vaf);
}
EXPORT_SYMBOL(__drm_printfn_line_dummy);
#endif
