#include_next <drm/display/drm_dp_helper.h>
#ifndef __BACKPORT_DRM_DP_HELPER_H__
#define __BACKPORT_DRM_DP_HELPER_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,16,0)
int drm_dp_link_symbol_cycles(int lane_count, int pixels, int dsc_slice_count,
			      int bpp_x16, int symbol_size, bool is_mst);
#endif

#endif /* __BACKPORT_DRM_DP_HELPER_H__ */
