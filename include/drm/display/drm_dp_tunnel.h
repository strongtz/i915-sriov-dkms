#include_next <drm/display/drm_dp_tunnel.h>
#ifndef __BACKPORT_DRM_DP_TUNNEL_H__
#define __BACKPORT_DRM_DP_TUNNEL_H__
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0)
static inline bool drm_dp_tunnel_pr_optimization_supported(const struct drm_dp_tunnel *tunnel)
{
	return false;
}
#endif
#endif