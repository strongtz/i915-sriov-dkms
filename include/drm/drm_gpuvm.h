#include_next <drm/drm_gpuvm.h>

#ifndef __BACKPORT_DRM_GPUVM_H__
#define __BACKPORT_DRM_GPUVM_H__
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
#define drm_gpuvm_bo_obtain_locked drm_gpuvm_bo_obtain
#endif
#endif
