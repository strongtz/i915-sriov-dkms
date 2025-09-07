#include_next <drm/display/drm_dp_helper.h>
#ifndef __BACKPORT_DRM_DP_HELPER_H__
#define __BACKPORT_DRM_DP_HELPER_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,15,0)
#define DP_EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_GRANT	    0x119   /* 1.4a */
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_GRANTED	    (1 << 0)

#define DP_EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_REQUEST	0x2211  /* 1.4a */
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_MASK		0xff
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_1_MS		0x00
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_20_MS	0x01
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_40_MS	0x02
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_60_MS	0x03
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_80_MS	0x04
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_100_MS	0x05

# define DP_EXTENDED_WAKE_TIMEOUT_REQUEST_MASK		0x7f
# define DP_EXTENDED_WAKE_TIMEOUT_GRANT			(1 << 7)

int drm_dp_lttpr_set_transparent_mode(struct drm_dp_aux *aux, bool enable);
int drm_dp_lttpr_init(struct drm_dp_aux *aux, int lttpr_count);

void drm_dp_lttpr_wake_timeout_setup(struct drm_dp_aux *aux, bool transparent_mode);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,16,0)
int drm_dp_link_symbol_cycles(int lane_count, int pixels, int dsc_slice_count,
			      int bpp_x16, int symbol_size, bool is_mst);
#endif

#endif /* __BACKPORT_DRM_DP_HELPER_H__ */
