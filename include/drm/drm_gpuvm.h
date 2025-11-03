#include_next <drm/drm_gpuvm.h>

#ifndef __BACKPORT_DRM_GPUVM_H__
#define __BACKPORT_DRM_GPUVM_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
#define DRM_GPUVA_OP_DRIVER ((enum drm_gpuva_op_type)(DRM_GPUVA_OP_PREFETCH + 1))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
struct drm_gpuvm_map_req {
	/**
	 * @op_map: struct drm_gpuva_op_map
	 */
	struct drm_gpuva_op_map map;
};

#define drm_gpuvm_madvise_ops_create LINUX_BACKPORT(drm_gpuvm_madvise_ops_create)
struct drm_gpuva_ops *
drm_gpuvm_madvise_ops_create(struct drm_gpuvm *gpuvm,
			     const struct drm_gpuvm_map_req *req);
#endif

#endif
