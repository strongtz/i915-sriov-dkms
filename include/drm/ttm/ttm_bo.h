#include_next <drm/ttm/ttm_bo.h>

#ifndef __BACKPORT_DRM_TTM_BO_H__
#define __BACKPORT_DRM_TTM_BO_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,14,0)
int ttm_bo_access(struct ttm_buffer_object *bo, unsigned long offset,
		  void *buf, int len, int write);
#endif

#endif
