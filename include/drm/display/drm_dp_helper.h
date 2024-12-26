#include <linux/version.h>
#include_next <drm/display/drm_dp_helper.h>

#ifndef _BACKPORT_DRM_DISPLAY_DRM_DP_HELPER_H
#define _BACKPORT_DRM_DISPLAY_DRM_DP_HELPER_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
/**
 * struct drm_dp_as_sdp - drm DP Adaptive Sync SDP
 *
 * This structure represents a DP AS SDP of drm
 * It is based on DP 2.1 spec [Table 2-126:  Adaptive-Sync SDP Header Bytes] and
 * [Table 2-127: Adaptive-Sync SDP Payload for DB0 through DB8]
 *
 * @sdp_type: Secondary-data packet type
 * @revision: Revision Number
 * @length: Number of valid data bytes
 * @vtotal: Minimum Vertical Vtotal
 * @target_rr: Target Refresh
 * @duration_incr_ms: Successive frame duration increase
 * @duration_decr_ms: Successive frame duration decrease
 * @operation_mode: Adaptive Sync Operation Mode
 */
struct drm_dp_as_sdp {
	unsigned char sdp_type;
	unsigned char revision;
	unsigned char length;
	int vtotal;
	int target_rr;
	int duration_incr_ms;
	int duration_decr_ms;
// 	bool target_rr_divider; // introduced in kernel 6.11 // TODO: use _compact suffix struct to make this field work in kernel below 6.11
	enum operation_mode mode;
};
void drm_dp_as_sdp_log(struct drm_printer *p,
		       const struct drm_dp_as_sdp *as_sdp);

static inline bool
drm_dp_128b132b_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_MAIN_LINK_CHANNEL_CODING] & DP_CAP_ANSI_128B132B;
}

bool drm_dp_as_sdp_supported(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE]);
#endif

#endif