#include_next <drm/drm_device.h>

#ifndef __BACKPORT_DRM_DEVICE_H__
#define __BACKPORT_DRM_DEVICE_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
#define DRM_WEDGE_RECOVERY_VENDOR	BIT(3)
#endif

#endif
