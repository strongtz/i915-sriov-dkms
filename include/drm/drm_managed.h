#include_next <drm/drm_managed.h>

#ifndef __BACKPORT_DRM_MANAGED_H__
#undef __BACKPORT_DRM_MANAGED_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
#define __drmm_workqueue_release LINUX_BACKPORT(__drmm_workqueue_release)
void __drmm_workqueue_release(struct drm_device *device, void *res);

/**
 * drmm_alloc_ordered_workqueue - &drm_device managed alloc_ordered_workqueue()
 * @dev: DRM device
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags (only WQ_FREEZABLE and WQ_MEM_RECLAIM are meaningful)
 * @args: args for @fmt
 *
 * This is a &drm_device-managed version of alloc_ordered_workqueue(). The
 * allocated workqueue is automatically destroyed on the final drm_dev_put().
 *
 * Returns: workqueue on success, negative ERR_PTR otherwise.
 */
#define drmm_alloc_ordered_workqueue(dev, fmt, flags, args...)					\
	({											\
		struct workqueue_struct *wq = alloc_ordered_workqueue(fmt, flags, ##args);	\
		wq ? ({										\
			int ret = drmm_add_action_or_reset(dev, __drmm_workqueue_release, wq);	\
			ret ? ERR_PTR(ret) : wq;						\
		}) : ERR_PTR(-ENOMEM);								\
	})
#endif
#endif
