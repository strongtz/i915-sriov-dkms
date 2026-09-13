#include_next <drm/display/drm_dp_helper.h>
#ifndef __BACKPORT_DRM_DP_HELPER_H__
#define __BACKPORT_DRM_DP_HELPER_H__


#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
#define drm_dp_dsc_sink_max_slice_throughput LINUX_BACKPORT(drm_dp_dsc_sink_max_slice_throughput)
int drm_dp_dsc_sink_max_slice_throughput(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
					 int peak_pixel_rate, bool is_rgb_yuv444);
#define drm_dp_dsc_branch_max_overall_throughput LINUX_BACKPORT(drm_dp_dsc_branch_max_overall_throughput)
int drm_dp_dsc_branch_max_overall_throughput(const u8 dsc_branch_dpcd[DP_DSC_BRANCH_CAP_SIZE],
					     bool is_rgb_yuv444);
#define drm_dp_dsc_branch_max_line_width LINUX_BACKPORT(drm_dp_dsc_branch_max_line_width)
int drm_dp_dsc_branch_max_line_width(const u8 dsc_branch_dpcd[DP_DSC_BRANCH_CAP_SIZE]);

#define DP_DPCD_QUIRK_DSC_THROUGHPUT_BPP_LIMIT ((enum drm_dp_quirk)(DP_DPCD_QUIRK_HBLANK_EXPANSION_REQUIRES_DSC + 1))

bool drm_dp_post_lt_adj_req_in_progress(const u8 link_status[DP_LINK_STATUS_SIZE]);

static inline bool
drm_dp_post_lt_adj_req_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x13 &&
		(dpcd[DP_MAX_LANE_COUNT] & DP_POST_LT_ADJ_REQ_SUPPORTED);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
#define drm_dp_dsc_slice_count_to_mask LINUX_BACKPORT(drm_dp_dsc_slice_count_to_mask)
u32 drm_dp_dsc_slice_count_to_mask(int slice_count);
#define drm_dp_dsc_sink_slice_count_mask LINUX_BACKPORT(drm_dp_dsc_sink_slice_count_mask)
u32 drm_dp_dsc_sink_slice_count_mask(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
				     bool is_edp);
#endif

#endif /* __BACKPORT_DRM_DP_HELPER_H__ */
