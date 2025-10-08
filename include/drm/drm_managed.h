#include_next <drm/drm_managed.h>

#ifndef __BACKPORT_DRM_MANAGED_H__
#undef __BACKPORT_DRM_MANAGED_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
void __drmm_workqueue_release(struct drm_device *device, void *res);
#endif

#endif
