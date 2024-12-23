#include <linux/version.h>
#include_next <drm/display/drm_dp_mst_helper.h>

#ifndef _BACKPORT_DRM_DISPLAY_DRM_DP_MST_HELPER_H
#define _BACKPORT_DRM_DISPLAY_DRM_DP_MST_HELPER_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
void drm_dp_mst_topology_queue_probe(struct drm_dp_mst_topology_mgr *mgr);
#endif

#endif