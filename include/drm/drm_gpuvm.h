#include_next <drm/drm_gpuvm.h>

#ifndef __BACKPORT_DRM_GPUVM_H__
#define __BACKPORT_DRM_GPUVM_H__

#define DRM_GPUVA_OP_DRIVER ((enum drm_gpuva_op_type)(DRM_GPUVA_OP_PREFETCH + 1))

#endif
