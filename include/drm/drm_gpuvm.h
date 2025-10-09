#include_next <drm/drm_gpuvm.h>

#ifndef __BACKPORT_DRM_GPUVM_H__
#define __BACKPORT_DRM_GPUVM_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
#define DRM_GPUVA_OP_DRIVER ((enum drm_gpuva_op_type)(DRM_GPUVA_OP_PREFETCH + 1))
#endif

#endif
