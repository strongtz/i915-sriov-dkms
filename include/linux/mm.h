#include_next <linux/mm.h>

#ifndef __BACKPORT_LINUX_MM_H__
#define __BACKPORT_LINUX_MM_H__

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
/*
 * Linux 7.1: zap_vma_ptes() was removed and split into two helpers:
 * zap_vma_range() (generic, NOT exported to modules) and
 * zap_special_vma_range() (for VM_PFNMAP/special mappings, EXPORT_SYMBOL_GPL).
 * The i915 tree vendored from 7.0 calls zap_vma_ptes() only on VM_PFNMAP VMAs
 * (EXPECTED_FLAGS contains VM_PFNMAP, see i915_mm.c) and ignores the return
 * value — so zap_special_vma_range() is the correct, linkable successor
 * (zap_vma_range would compile but fail at MODPOST as undefined, because it is
 * not exported).
 */
static inline void zap_vma_ptes(struct vm_area_struct *vma,
				unsigned long address, unsigned long size)
{
	zap_special_vma_range(vma, address, size);
}
#endif

#endif /* __BACKPORT_LINUX_MM_H__ */
