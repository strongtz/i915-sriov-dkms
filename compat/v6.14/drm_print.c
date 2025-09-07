/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <drm/drm_print.h>
#include <drm/drm_device.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,14,0)
/**
 * drm_print_hex_dump - print a hex dump to a &drm_printer stream
 * @p: The &drm_printer
 * @prefix: Prefix for each line, may be NULL for no prefix
 * @buf: Buffer to dump
 * @len: Length of buffer
 *
 * Print hex dump to &drm_printer, with 16 space-separated hex bytes per line,
 * optionally with a prefix on each line. No separator is added after prefix.
 */
void drm_print_hex_dump(struct drm_printer *p, const char *prefix,
			const u8 *buf, size_t len)
{
	int i;

	for (i = 0; i < len; i += 16) {
		int bytes_per_line = min(16, len - i);

		drm_printf(p, "%s%*ph\n", prefix ?: "", bytes_per_line, buf + i);
	}
}
EXPORT_SYMBOL(drm_print_hex_dump);
#endif
