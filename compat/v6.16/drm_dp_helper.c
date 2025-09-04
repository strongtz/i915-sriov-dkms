/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/display/drm_dp_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,16,0)
/* See DP Standard v2.1 2.6.4.4.1.1, 2.8.4.4, 2.8.7 */
static int drm_dp_link_data_symbol_cycles(int lane_count, int pixels,
					  int bpp_x16, int symbol_size,
					  bool is_mst)
{
	int cycles = DIV_ROUND_UP(pixels * bpp_x16, 16 * symbol_size * lane_count);
	int align = is_mst ? 4 / lane_count : 1;

	return ALIGN(cycles, align);
}

/**
 * drm_dp_link_symbol_cycles - calculate the link symbol count with/without dsc
 * @lane_count: DP link lane count
 * @pixels: number of pixels in a scanline
 * @dsc_slice_count: number of slices for DSC or '0' for non-DSC
 * @bpp_x16: bits per pixel in .4 binary fixed format
 * @symbol_size: DP symbol size
 * @is_mst: %true for MST and %false for SST
 *
 * Calculate the link symbol cycles for both DSC (@dsc_slice_count !=0) and
 * non-DSC case (@dsc_slice_count == 0) and return the count.
 */
int drm_dp_link_symbol_cycles(int lane_count, int pixels, int dsc_slice_count,
			      int bpp_x16, int symbol_size, bool is_mst)
{
	int slice_count = dsc_slice_count ? : 1;
	int slice_pixels = DIV_ROUND_UP(pixels, slice_count);
	int slice_data_cycles = drm_dp_link_data_symbol_cycles(lane_count,
							       slice_pixels,
							       bpp_x16,
							       symbol_size,
							       is_mst);
	int slice_eoc_cycles = 0;

	if (dsc_slice_count)
		slice_eoc_cycles = is_mst ? 4 / lane_count : 1;

	return slice_count * (slice_data_cycles + slice_eoc_cycles);
}
EXPORT_SYMBOL(drm_dp_link_symbol_cycles);
#endif
