/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/ttm/ttm_bo.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
int ttm_bo_setup_export(struct ttm_buffer_object *bo,
			struct ttm_operation_ctx *ctx)
{
	int ret;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret != 0)
		return ret;

	ret = ttm_bo_populate(bo, ctx);
	ttm_bo_unreserve(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_setup_export);
#endif
