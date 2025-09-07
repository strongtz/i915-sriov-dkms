// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright © 2024 Intel Corporation
 *
 * Authors:
 *     Matthew Brost <matthew.brost@intel.com>
 */

#include <linux/mm.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,15,0) && LINUX_VERSION_CODE < KERNEL_VERSION(6,17,0)
#include <drm/drm_gpusvm.h>
/**
 * drm_gpusvm_find_vma_start() - Find start address for first VMA in range
 * @gpusvm: Pointer to the GPU SVM structure
 * @start: The inclusive start user address.
 * @end: The exclusive end user address.
 *
 * Returns: The start address of first VMA within the provided range,
 * ULONG_MAX otherwise. Assumes start_addr < end_addr.
 */
unsigned long
drm_gpusvm_find_vma_start(struct drm_gpusvm *gpusvm,
			  unsigned long start,
			  unsigned long end)
{
	struct mm_struct *mm = gpusvm->mm;
	struct vm_area_struct *vma;
	unsigned long addr = ULONG_MAX;

	if (!mmget_not_zero(mm))
		return addr;

	mmap_read_lock(mm);

	vma = find_vma_intersection(mm, start, end);
	if (vma)
		addr =  vma->vm_start;

	mmap_read_unlock(mm);
	mmput(mm);

	return addr;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_find_vma_start);
#endif
