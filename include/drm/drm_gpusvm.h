#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,15,0)
#include_next <drm/drm_gpusvm.h>
#ifndef __BACKPORT_DRM_GPUSVM_H__
#define __BACKPORT_DRM_GPUSVM_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,17,0)
unsigned long
drm_gpusvm_find_vma_start(struct drm_gpusvm *gpusvm,
			  unsigned long start,
			  unsigned long end);
#endif

#endif /* __BACKPORT_DRM_GPUSVM_H__ */
#endif
