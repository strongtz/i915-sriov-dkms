#include <drm/display/drm_dp_mst_helper.h>

int main(void)
{
  return (int)drm_dp_add_payload_part2((struct drm_dp_mst_topology_mgr *)(0xdeadbeef), (struct drm_dp_mst_atomic_payload *)(0xdeadbeef));
}

// build success: #define _CONFIGURE_DRM_DP_ADD_PAYLOAD_PART2_VERSION KERNEL_VERSION(6, 10, 0)