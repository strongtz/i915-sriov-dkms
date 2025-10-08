/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_managed.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
void __drmm_workqueue_release(struct drm_device *device, void *res)
{
	struct workqueue_struct *wq = res;

	destroy_workqueue(wq);
}
EXPORT_SYMBOL(__drmm_workqueue_release);
#endif
