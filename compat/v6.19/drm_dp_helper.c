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

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_print.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
/*
 * See DP Standard v2.1a 2.8.4 Minimum Slices/Display, Table 2-159 and
 * Appendix L.1 Derivation of Slice Count Requirements.
 */
static int dsc_sink_min_slice_throughput(int peak_pixel_rate)
{
	if (peak_pixel_rate >= 4800000)
		return 600000;
	else if (peak_pixel_rate >= 2700000)
		return 400000;
	else
		return 340000;
}

/**
 * drm_dp_dsc_sink_max_slice_throughput() - Get a DSC sink's maximum pixel throughput per slice
 * @dsc_dpcd: DSC sink's capabilities from DPCD
 * @peak_pixel_rate: Cumulative peak pixel rate in kHz
 * @is_rgb_yuv444: The mode is either RGB or YUV444
 *
 * Return the DSC sink device's maximum pixel throughput per slice, based on
 * the device's @dsc_dpcd capabilities, the @peak_pixel_rate of the transferred
 * stream(s) and whether the output format @is_rgb_yuv444 or yuv422/yuv420.
 *
 * Note that @peak_pixel_rate is the total pixel rate transferred to the same
 * DSC/display sink. For instance to calculate a tile's slice count of an MST
 * multi-tiled display sink (not considering here the required
 * rounding/alignment of slice count)::
 *
 *   @peak_pixel_rate = tile_pixel_rate * tile_count
 *   total_slice_count = @peak_pixel_rate / drm_dp_dsc_sink_max_slice_throughput(@peak_pixel_rate)
 *   tile_slice_count = total_slice_count / tile_count
 *
 * Returns:
 * The maximum pixel throughput per slice supported by the DSC sink device
 * in kPixels/sec.
 */
int drm_dp_dsc_sink_max_slice_throughput(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
					 int peak_pixel_rate, bool is_rgb_yuv444)
{
	int throughput;
	int delta = 0;
	int base;

	throughput = dsc_dpcd[DP_DSC_PEAK_THROUGHPUT - DP_DSC_SUPPORT];

	if (is_rgb_yuv444) {
		throughput = (throughput & DP_DSC_THROUGHPUT_MODE_0_MASK) >>
			     DP_DSC_THROUGHPUT_MODE_0_SHIFT;

		delta = ((dsc_dpcd[DP_DSC_RC_BUF_BLK_SIZE - DP_DSC_SUPPORT]) &
			 DP_DSC_THROUGHPUT_MODE_0_DELTA_MASK) >>
			DP_DSC_THROUGHPUT_MODE_0_DELTA_SHIFT;	/* in units of 2 MPixels/sec */
		delta *= 2000;
	} else {
		throughput = (throughput & DP_DSC_THROUGHPUT_MODE_1_MASK) >>
			     DP_DSC_THROUGHPUT_MODE_1_SHIFT;
	}

	switch (throughput) {
	case 0:
		return dsc_sink_min_slice_throughput(peak_pixel_rate);
	case 1:
		base = 340000;
		break;
	case 2 ... 14:
		base = 400000 + 50000 * (throughput - 2);
		break;
	case 15:
		base = 170000;
		break;
	}

	return base + delta;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_max_slice_throughput);

static u8 dsc_branch_dpcd_cap(const u8 dpcd[DP_DSC_BRANCH_CAP_SIZE], int reg)
{
	return dpcd[reg - DP_DSC_BRANCH_OVERALL_THROUGHPUT_0];
}

/**
 * drm_dp_dsc_branch_max_overall_throughput() - Branch device's max overall DSC pixel throughput
 * @dsc_branch_dpcd: DSC branch capabilities from DPCD
 * @is_rgb_yuv444: The mode is either RGB or YUV444
 *
 * Return the branch device's maximum overall DSC pixel throughput, based on
 * the device's DPCD DSC branch capabilities, and whether the output
 * format @is_rgb_yuv444 or yuv422/yuv420.
 *
 * Returns:
 * - 0:   The maximum overall throughput capability is not indicated by
 *        the device separately and it must be determined from the per-slice
 *        max throughput (see @drm_dp_dsc_branch_slice_max_throughput())
 *        and the maximum slice count supported by the device.
 * - > 0: The maximum overall DSC pixel throughput supported by the branch
 *        device in kPixels/sec.
 */
int drm_dp_dsc_branch_max_overall_throughput(const u8 dsc_branch_dpcd[DP_DSC_BRANCH_CAP_SIZE],
					     bool is_rgb_yuv444)
{
	int throughput;

	if (is_rgb_yuv444)
		throughput = dsc_branch_dpcd_cap(dsc_branch_dpcd,
						 DP_DSC_BRANCH_OVERALL_THROUGHPUT_0);
	else
		throughput = dsc_branch_dpcd_cap(dsc_branch_dpcd,
						 DP_DSC_BRANCH_OVERALL_THROUGHPUT_1);

	switch (throughput) {
	case 0:
		return 0;
	case 1:
		return 680000;
	default:
		return 600000 + 50000 * throughput;
	}
}
EXPORT_SYMBOL(drm_dp_dsc_branch_max_overall_throughput);

/**
 * drm_dp_dsc_branch_max_line_width() - Branch device's max DSC line width
 * @dsc_branch_dpcd: DSC branch capabilities from DPCD
 *
 * Return the branch device's maximum overall DSC line width, based on
 * the device's @dsc_branch_dpcd capabilities.
 *
 * Returns:
 * - 0:        The maximum line width is not indicated by the device
 *             separately and it must be determined from the maximum
 *             slice count and slice-width supported by the device.
 * - %-EINVAL: The device indicates an invalid maximum line width
 *             (< 5120 pixels).
 * - >= 5120:  The maximum line width in pixels.
 */
int drm_dp_dsc_branch_max_line_width(const u8 dsc_branch_dpcd[DP_DSC_BRANCH_CAP_SIZE])
{
	int line_width = dsc_branch_dpcd_cap(dsc_branch_dpcd, DP_DSC_BRANCH_MAX_LINE_WIDTH);

	switch (line_width) {
	case 0:
		return 0;
	case 1 ... 15:
		return -EINVAL;
	default:
		return line_width * 320;
	}
}
EXPORT_SYMBOL(drm_dp_dsc_branch_max_line_width);
#endif
