/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 14, 0)
static int ttm_bo_vm_access_kmap(struct ttm_buffer_object *bo,
				 unsigned long offset,
				 uint8_t *buf, int len, int write)
{
	unsigned long page = offset >> PAGE_SHIFT;
	unsigned long bytes_left = len;
	int ret;

	/* Copy a page at a time, that way no extra virtual address
	 * mapping is needed
	 */
	offset -= page << PAGE_SHIFT;
	do {
		unsigned long bytes = min(bytes_left, PAGE_SIZE - offset);
		struct ttm_bo_kmap_obj map;
		void *ptr;
		bool is_iomem;

		ret = ttm_bo_kmap(bo, page, 1, &map);
		if (ret)
			return ret;

		ptr = (uint8_t *)ttm_kmap_obj_virtual(&map, &is_iomem) + offset;
		WARN_ON_ONCE(is_iomem);
		if (write)
			memcpy(ptr, buf, bytes);
		else
			memcpy(buf, ptr, bytes);
		ttm_bo_kunmap(&map);

		page++;
		buf += bytes;
		bytes_left -= bytes;
		offset = 0;
	} while (bytes_left);

	return len;
}

/**
 * ttm_bo_access - Helper to access a buffer object
 *
 * @bo: ttm buffer object
 * @offset: access offset into buffer object
 * @buf: pointer to caller memory to read into or write from
 * @len: length of access
 * @write: write access
 *
 * Utility function to access a buffer object. Useful when buffer object cannot
 * be easily mapped (non-contiguous, non-visible, etc...). Should not directly
 * be exported to user space via a peak / poke interface.
 *
 * Returns:
 * @len if successful, negative error code on failure.
 */
int ttm_bo_access(struct ttm_buffer_object *bo, unsigned long offset,
		  void *buf, int len, int write)
{
	int ret;

	if (len < 1 || (offset + len) > bo->base.size)
		return -EIO;

	ret = ttm_bo_reserve(bo, true, false, NULL);
	if (ret)
		return ret;

	switch (bo->resource->mem_type) {
	case TTM_PL_SYSTEM:
		fallthrough;
	case TTM_PL_TT:
		ret = ttm_bo_vm_access_kmap(bo, offset, buf, len, write);
		break;
	default:
		if (bo->bdev->funcs->access_memory)
			ret = bo->bdev->funcs->access_memory
				(bo, offset, buf, len, write);
		else
			ret = -EIO;
	}

	ttm_bo_unreserve(bo);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_access);
#endif
