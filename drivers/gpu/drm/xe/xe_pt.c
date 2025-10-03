// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/dma-fence-array.h>

#include "xe_pt.h"

#include "regs/xe_gtt_defs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_migrate.h"
#include "xe_pt_types.h"
#include "xe_pt_walk.h"
#include "xe_res_cursor.h"
#include "xe_sched_job.h"
#include "xe_sync.h"
#include "xe_svm.h"
#include "xe_trace.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_vm.h"

struct xe_pt_dir {
	struct xe_pt pt;
	/** @children: Array of page-table child nodes */
	struct xe_ptw *children[XE_PDES];
	/** @staging: Array of page-table staging nodes */
	struct xe_ptw *staging[XE_PDES];
};

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM)
#define xe_pt_set_addr(__xe_pt, __addr) ((__xe_pt)->addr = (__addr))
#define xe_pt_addr(__xe_pt) ((__xe_pt)->addr)
#else
#define xe_pt_set_addr(__xe_pt, __addr)
#define xe_pt_addr(__xe_pt) 0ull
#endif

static const u64 xe_normal_pt_shifts[] = {12, 21, 30, 39, 48};
static const u64 xe_compact_pt_shifts[] = {16, 21, 30, 39, 48};

#define XE_PT_HIGHEST_LEVEL (ARRAY_SIZE(xe_normal_pt_shifts) - 1)

static struct xe_pt_dir *as_xe_pt_dir(struct xe_pt *pt)
{
	return container_of(pt, struct xe_pt_dir, pt);
}

static struct xe_pt *
xe_pt_entry_staging(struct xe_pt_dir *pt_dir, unsigned int index)
{
	return container_of(pt_dir->staging[index], struct xe_pt, base);
}

static u64 __xe_pt_empty_pte(struct xe_tile *tile, struct xe_vm *vm,
			     unsigned int level)
{
	struct xe_device *xe = tile_to_xe(tile);
	u16 pat_index = xe->pat.idx[XE_CACHE_WB];
	u8 id = tile->id;

	if (!xe_vm_has_scratch(vm))
		return 0;

	if (level > MAX_HUGEPTE_LEVEL)
		return vm->pt_ops->pde_encode_bo(vm->scratch_pt[id][level - 1]->bo,
						 0, pat_index);

	return vm->pt_ops->pte_encode_addr(xe, 0, pat_index, level, IS_DGFX(xe), 0) |
		XE_PTE_NULL;
}

static void xe_pt_free(struct xe_pt *pt)
{
	if (pt->level)
		kfree(as_xe_pt_dir(pt));
	else
		kfree(pt);
}

/**
 * xe_pt_create() - Create a page-table.
 * @vm: The vm to create for.
 * @tile: The tile to create for.
 * @level: The page-table level.
 *
 * Allocate and initialize a single struct xe_pt metadata structure. Also
 * create the corresponding page-table bo, but don't initialize it. If the
 * level is grater than zero, then it's assumed to be a directory page-
 * table and the directory structure is also allocated and initialized to
 * NULL pointers.
 *
 * Return: A valid struct xe_pt pointer on success, Pointer error code on
 * error.
 */
struct xe_pt *xe_pt_create(struct xe_vm *vm, struct xe_tile *tile,
			   unsigned int level)
{
	struct xe_pt *pt;
	struct xe_bo *bo;
	u32 bo_flags;
	int err;

	if (level) {
		struct xe_pt_dir *dir = kzalloc(sizeof(*dir), GFP_KERNEL);

		pt = (dir) ? &dir->pt : NULL;
	} else {
		pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	}
	if (!pt)
		return ERR_PTR(-ENOMEM);

	bo_flags = XE_BO_FLAG_VRAM_IF_DGFX(tile) |
		   XE_BO_FLAG_IGNORE_MIN_PAGE_SIZE |
		   XE_BO_FLAG_NO_RESV_EVICT | XE_BO_FLAG_PAGETABLE;
	if (vm->xef) /* userspace */
		bo_flags |= XE_BO_FLAG_PINNED_LATE_RESTORE;

	pt->level = level;
	bo = xe_bo_create_pin_map(vm->xe, tile, vm, SZ_4K,
				  ttm_bo_type_kernel,
				  bo_flags);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto err_kfree;
	}
	pt->bo = bo;
	pt->base.children = level ? as_xe_pt_dir(pt)->children : NULL;
	pt->base.staging = level ? as_xe_pt_dir(pt)->staging : NULL;

	if (vm->xef)
		xe_drm_client_add_bo(vm->xef->client, pt->bo);
	xe_tile_assert(tile, level <= XE_VM_MAX_LEVEL);

	return pt;

err_kfree:
	xe_pt_free(pt);
	return ERR_PTR(err);
}
ALLOW_ERROR_INJECTION(xe_pt_create, ERRNO);

/**
 * xe_pt_populate_empty() - Populate a page-table bo with scratch- or zero
 * entries.
 * @tile: The tile the scratch pagetable of which to use.
 * @vm: The vm we populate for.
 * @pt: The pagetable the bo of which to initialize.
 *
 * Populate the page-table bo of @pt with entries pointing into the tile's
 * scratch page-table tree if any. Otherwise populate with zeros.
 */
void xe_pt_populate_empty(struct xe_tile *tile, struct xe_vm *vm,
			  struct xe_pt *pt)
{
	struct iosys_map *map = &pt->bo->vmap;
	u64 empty;
	int i;

	if (!xe_vm_has_scratch(vm)) {
		/*
		 * FIXME: Some memory is allocated already allocated to zero?
		 * Find out which memory that is and avoid this memset...
		 */
		xe_map_memset(vm->xe, map, 0, 0, SZ_4K);
	} else {
		empty = __xe_pt_empty_pte(tile, vm, pt->level);
		for (i = 0; i < XE_PDES; i++)
			xe_pt_write(vm->xe, map, i, empty);
	}
}

/**
 * xe_pt_shift() - Return the ilog2 value of the size of the address range of
 * a page-table at a certain level.
 * @level: The level.
 *
 * Return: The ilog2 value of the size of the address range of a page-table
 * at level @level.
 */
unsigned int xe_pt_shift(unsigned int level)
{
	return XE_PTE_SHIFT + XE_PDE_SHIFT * level;
}

/**
 * xe_pt_destroy() - Destroy a page-table tree.
 * @pt: The root of the page-table tree to destroy.
 * @flags: vm flags. Currently unused.
 * @deferred: List head of lockless list for deferred putting. NULL for
 *            immediate putting.
 *
 * Puts the page-table bo, recursively calls xe_pt_destroy on all children
 * and finally frees @pt. TODO: Can we remove the @flags argument?
 */
void xe_pt_destroy(struct xe_pt *pt, u32 flags, struct llist_head *deferred)
{
	int i;

	if (!pt)
		return;

	XE_WARN_ON(!list_empty(&pt->bo->ttm.base.gpuva.list));
	xe_bo_unpin(pt->bo);
	xe_bo_put_deferred(pt->bo, deferred);

	if (pt->level > 0 && pt->num_live) {
		struct xe_pt_dir *pt_dir = as_xe_pt_dir(pt);

		for (i = 0; i < XE_PDES; i++) {
			if (xe_pt_entry_staging(pt_dir, i))
				xe_pt_destroy(xe_pt_entry_staging(pt_dir, i), flags,
					      deferred);
		}
	}
	xe_pt_free(pt);
}

/**
 * xe_pt_clear() - Clear a page-table.
 * @xe: xe device.
 * @pt: The page-table.
 *
 * Clears page-table by setting to zero.
 */
void xe_pt_clear(struct xe_device *xe, struct xe_pt *pt)
{
	struct iosys_map *map = &pt->bo->vmap;

	xe_map_memset(xe, map, 0, 0, SZ_4K);
}

/**
 * DOC: Pagetable building
 *
 * Below we use the term "page-table" for both page-directories, containing
 * pointers to lower level page-directories or page-tables, and level 0
 * page-tables that contain only page-table-entries pointing to memory pages.
 *
 * When inserting an address range in an already existing page-table tree
 * there will typically be a set of page-tables that are shared with other
 * address ranges, and a set that are private to this address range.
 * The set of shared page-tables can be at most two per level,
 * and those can't be updated immediately because the entries of those
 * page-tables may still be in use by the gpu for other mappings. Therefore
 * when inserting entries into those, we instead stage those insertions by
 * adding insertion data into struct xe_vm_pgtable_update structures. This
 * data, (subtrees for the cpu and page-table-entries for the gpu) is then
 * added in a separate commit step. CPU-data is committed while still under the
 * vm lock, the object lock and for userptr, the notifier lock in read mode.
 * The GPU async data is committed either by the GPU or CPU after fulfilling
 * relevant dependencies.
 * For non-shared page-tables (and, in fact, for shared ones that aren't
 * existing at the time of staging), we add the data in-place without the
 * special update structures. This private part of the page-table tree will
 * remain disconnected from the vm page-table tree until data is committed to
 * the shared page tables of the vm tree in the commit phase.
 */

struct xe_pt_update {
	/** @update: The update structure we're building for this parent. */
	struct xe_vm_pgtable_update *update;
	/** @parent: The parent. Used to detect a parent change. */
	struct xe_pt *parent;
	/** @preexisting: Whether the parent was pre-existing or allocated */
	bool preexisting;
};

/**
 * struct xe_pt_stage_bind_walk - Walk state for the stage_bind walk.
 */
struct xe_pt_stage_bind_walk {
	/** @base: The base class. */
	struct xe_pt_walk base;

	/* Input parameters for the walk */
	/** @vm: The vm we're building for. */
	struct xe_vm *vm;
	/** @tile: The tile we're building for. */
	struct xe_tile *tile;
	/** @default_vram_pte: PTE flag only template for VRAM. No address is associated */
	u64 default_vram_pte;
	/** @default_system_pte: PTE flag only template for System. No address is associated */
	u64 default_system_pte;
	/** @dma_offset: DMA offset to add to the PTE. */
	u64 dma_offset;
	/**
	 * @needs_64K: This address range enforces 64K alignment and
	 * granularity on VRAM.
	 */
	bool needs_64K;
	/** @clear_pt: clear page table entries during the bind walk */
	bool clear_pt;
	/**
	 * @vma: VMA being mapped
	 */
	struct xe_vma *vma;

	/* Also input, but is updated during the walk*/
	/** @curs: The DMA address cursor. */
	struct xe_res_cursor *curs;
	/** @va_curs_start: The Virtual address corresponding to @curs->start */
	u64 va_curs_start;

	/* Output */
	/** @wupd: Walk output data for page-table updates. */
	struct xe_walk_update {
		/** @wupd.entries: Caller provided storage. */
		struct xe_vm_pgtable_update *entries;
		/** @wupd.num_used_entries: Number of update @entries used. */
		unsigned int num_used_entries;
		/** @wupd.updates: Tracks the update entry at a given level */
		struct xe_pt_update updates[XE_VM_MAX_LEVEL + 1];
	} wupd;

	/* Walk state */
	/**
	 * @l0_end_addr: The end address of the current l0 leaf. Used for
	 * 64K granularity detection.
	 */
	u64 l0_end_addr;
	/** @addr_64K: The start address of the current 64K chunk. */
	u64 addr_64K;
	/** @found_64K: Whether @add_64K actually points to a 64K chunk. */
	bool found_64K;
};

static int
xe_pt_new_shared(struct xe_walk_update *wupd, struct xe_pt *parent,
		 pgoff_t offset, bool alloc_entries)
{
	struct xe_pt_update *upd = &wupd->updates[parent->level];
	struct xe_vm_pgtable_update *entry;

	/*
	 * For *each level*, we could only have one active
	 * struct xt_pt_update at any one time. Once we move on to a
	 * new parent and page-directory, the old one is complete, and
	 * updates are either already stored in the build tree or in
	 * @wupd->entries
	 */
	if (likely(upd->parent == parent))
		return 0;

	upd->parent = parent;
	upd->preexisting = true;

	if (wupd->num_used_entries == XE_VM_MAX_LEVEL * 2 + 1)
		return -EINVAL;

	entry = wupd->entries + wupd->num_used_entries++;
	upd->update = entry;
	entry->ofs = offset;
	entry->pt_bo = parent->bo;
	entry->pt = parent;
	entry->flags = 0;
	entry->qwords = 0;
	entry->pt_bo->update_index = -1;

	if (alloc_entries) {
		entry->pt_entries = kmalloc_array(XE_PDES,
						  sizeof(*entry->pt_entries),
						  GFP_KERNEL);
		if (!entry->pt_entries)
			return -ENOMEM;
	}

	return 0;
}

/*
 * NOTE: This is a very frequently called function so we allow ourselves
 * to annotate (using branch prediction hints) the fastpath of updating a
 * non-pre-existing pagetable with leaf ptes.
 */
static int
xe_pt_insert_entry(struct xe_pt_stage_bind_walk *xe_walk, struct xe_pt *parent,
		   pgoff_t offset, struct xe_pt *xe_child, u64 pte)
{
	struct xe_pt_update *upd = &xe_walk->wupd.updates[parent->level];
	struct xe_pt_update *child_upd = xe_child ?
		&xe_walk->wupd.updates[xe_child->level] : NULL;
	int ret;

	ret = xe_pt_new_shared(&xe_walk->wupd, parent, offset, true);
	if (unlikely(ret))
		return ret;

	/*
	 * Register this new pagetable so that it won't be recognized as
	 * a shared pagetable by a subsequent insertion.
	 */
	if (unlikely(child_upd)) {
		child_upd->update = NULL;
		child_upd->parent = xe_child;
		child_upd->preexisting = false;
	}

	if (likely(!upd->preexisting)) {
		/* Continue building a non-connected subtree. */
		struct iosys_map *map = &parent->bo->vmap;

		if (unlikely(xe_child)) {
			parent->base.children[offset] = &xe_child->base;
			parent->base.staging[offset] = &xe_child->base;
		}

		xe_pt_write(xe_walk->vm->xe, map, offset, pte);
		parent->num_live++;
	} else {
		/* Shared pt. Stage update. */
		unsigned int idx;
		struct xe_vm_pgtable_update *entry = upd->update;

		idx = offset - entry->ofs;
		entry->pt_entries[idx].pt = xe_child;
		entry->pt_entries[idx].pte = pte;
		entry->qwords++;
	}

	return 0;
}

static bool xe_pt_hugepte_possible(u64 addr, u64 next, unsigned int level,
				   struct xe_pt_stage_bind_walk *xe_walk)
{
	u64 size, dma;

	if (level > MAX_HUGEPTE_LEVEL)
		return false;

	/* Does the virtual range requested cover a huge pte? */
	if (!xe_pt_covers(addr, next, level, &xe_walk->base))
		return false;

	/* Does the DMA segment cover the whole pte? */
	if (next - xe_walk->va_curs_start > xe_walk->curs->size)
		return false;

	/* null VMA's do not have dma addresses */
	if (xe_vma_is_null(xe_walk->vma))
		return true;

	/* if we are clearing page table, no dma addresses*/
	if (xe_walk->clear_pt)
		return true;

	/* Is the DMA address huge PTE size aligned? */
	size = next - addr;
	dma = addr - xe_walk->va_curs_start + xe_res_dma(xe_walk->curs);

	return IS_ALIGNED(dma, size);
}

/*
 * Scan the requested mapping to check whether it can be done entirely
 * with 64K PTEs.
 */
static bool
xe_pt_scan_64K(u64 addr, u64 next, struct xe_pt_stage_bind_walk *xe_walk)
{
	struct xe_res_cursor curs = *xe_walk->curs;

	if (!IS_ALIGNED(addr, SZ_64K))
		return false;

	if (next > xe_walk->l0_end_addr)
		return false;

	/* null VMA's do not have dma addresses */
	if (xe_vma_is_null(xe_walk->vma))
		return true;

	xe_res_next(&curs, addr - xe_walk->va_curs_start);
	for (; addr < next; addr += SZ_64K) {
		if (!IS_ALIGNED(xe_res_dma(&curs), SZ_64K) || curs.size < SZ_64K)
			return false;

		xe_res_next(&curs, SZ_64K);
	}

	return addr == next;
}

/*
 * For non-compact "normal" 4K level-0 pagetables, we want to try to group
 * addresses together in 64K-contigous regions to add a 64K TLB hint for the
 * device to the PTE.
 * This function determines whether the address is part of such a
 * segment. For VRAM in normal pagetables, this is strictly necessary on
 * some devices.
 */
static bool
xe_pt_is_pte_ps64K(u64 addr, u64 next, struct xe_pt_stage_bind_walk *xe_walk)
{
	/* Address is within an already found 64k region */
	if (xe_walk->found_64K && addr - xe_walk->addr_64K < SZ_64K)
		return true;

	xe_walk->found_64K = xe_pt_scan_64K(addr, addr + SZ_64K, xe_walk);
	xe_walk->addr_64K = addr;

	return xe_walk->found_64K;
}

static int
xe_pt_stage_bind_entry(struct xe_ptw *parent, pgoff_t offset,
		       unsigned int level, u64 addr, u64 next,
		       struct xe_ptw **child,
		       enum page_walk_action *action,
		       struct xe_pt_walk *walk)
{
	struct xe_pt_stage_bind_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	u16 pat_index = xe_walk->vma->pat_index;
	struct xe_pt *xe_parent = container_of(parent, typeof(*xe_parent), base);
	struct xe_vm *vm = xe_walk->vm;
	struct xe_pt *xe_child;
	bool covers;
	int ret = 0;
	u64 pte;

	/* Is this a leaf entry ?*/
	if (level == 0 || xe_pt_hugepte_possible(addr, next, level, xe_walk)) {
		struct xe_res_cursor *curs = xe_walk->curs;
		bool is_null = xe_vma_is_null(xe_walk->vma);
		bool is_vram = is_null ? false : xe_res_is_vram(curs);

		XE_WARN_ON(xe_walk->va_curs_start != addr);

		if (xe_walk->clear_pt) {
			pte = 0;
		} else {
			pte = vm->pt_ops->pte_encode_vma(is_null ? 0 :
							 xe_res_dma(curs) +
							 xe_walk->dma_offset,
							 xe_walk->vma,
							 pat_index, level);
			if (!is_null)
				pte |= is_vram ? xe_walk->default_vram_pte :
					xe_walk->default_system_pte;

			/*
			 * Set the XE_PTE_PS64 hint if possible, otherwise if
			 * this device *requires* 64K PTE size for VRAM, fail.
			 */
			if (level == 0 && !xe_parent->is_compact) {
				if (xe_pt_is_pte_ps64K(addr, next, xe_walk)) {
					xe_walk->vma->gpuva.flags |=
							XE_VMA_PTE_64K;
					pte |= XE_PTE_PS64;
				} else if (XE_WARN_ON(xe_walk->needs_64K &&
					   is_vram)) {
					return -EINVAL;
				}
			}
		}

		ret = xe_pt_insert_entry(xe_walk, xe_parent, offset, NULL, pte);
		if (unlikely(ret))
			return ret;

		if (!is_null && !xe_walk->clear_pt)
			xe_res_next(curs, next - addr);
		xe_walk->va_curs_start = next;
		xe_walk->vma->gpuva.flags |= (XE_VMA_PTE_4K << level);
		*action = ACTION_CONTINUE;

		return ret;
	}

	/*
	 * Descending to lower level. Determine if we need to allocate a
	 * new page table or -directory, which we do if there is no
	 * previous one or there is one we can completely replace.
	 */
	if (level == 1) {
		walk->shifts = xe_normal_pt_shifts;
		xe_walk->l0_end_addr = next;
	}

	covers = xe_pt_covers(addr, next, level, &xe_walk->base);
	if (covers || !*child) {
		u64 flags = 0;

		xe_child = xe_pt_create(xe_walk->vm, xe_walk->tile, level - 1);
		if (IS_ERR(xe_child))
			return PTR_ERR(xe_child);

		xe_pt_set_addr(xe_child,
			       round_down(addr, 1ull << walk->shifts[level]));

		if (!covers)
			xe_pt_populate_empty(xe_walk->tile, xe_walk->vm, xe_child);

		*child = &xe_child->base;

		/*
		 * Prefer the compact pagetable layout for L0 if possible. Only
		 * possible if VMA covers entire 2MB region as compact 64k and
		 * 4k pages cannot be mixed within a 2MB region.
		 * TODO: Suballocate the pt bo to avoid wasting a lot of
		 * memory.
		 */
		if (GRAPHICS_VERx100(tile_to_xe(xe_walk->tile)) >= 1250 && level == 1 &&
		    covers && xe_pt_scan_64K(addr, next, xe_walk)) {
			walk->shifts = xe_compact_pt_shifts;
			xe_walk->vma->gpuva.flags |= XE_VMA_PTE_COMPACT;
			flags |= XE_PDE_64K;
			xe_child->is_compact = true;
		}

		pte = vm->pt_ops->pde_encode_bo(xe_child->bo, 0, pat_index) | flags;
		ret = xe_pt_insert_entry(xe_walk, xe_parent, offset, xe_child,
					 pte);
	}

	*action = ACTION_SUBTREE;
	return ret;
}

static const struct xe_pt_walk_ops xe_pt_stage_bind_ops = {
	.pt_entry = xe_pt_stage_bind_entry,
};

/*
 * Default atomic expectations for different allocation scenarios are as follows:
 *
 * 1. Traditional API: When the VM is not in LR mode:
 *    - Device atomics are expected to function with all allocations.
 *
 * 2. Compute/SVM API: When the VM is in LR mode:
 *    - Device atomics are the default behavior when the bo is placed in a single region.
 *    - In all other cases device atomics will be disabled with AE=0 until an application
 *      request differently using a ioctl like madvise.
 */
static bool xe_atomic_for_vram(struct xe_vm *vm)
{
	return true;
}

static bool xe_atomic_for_system(struct xe_vm *vm, struct xe_bo *bo)
{
	struct xe_device *xe = vm->xe;

	if (!xe->info.has_device_atomics_on_smem)
		return false;

	/*
	 * If a SMEM+LMEM allocation is backed by SMEM, a device
	 * atomics will cause a gpu page fault and which then
	 * gets migrated to LMEM, bind such allocations with
	 * device atomics enabled.
	 *
	 * TODO: Revisit this. Perhaps add something like a
	 * fault_on_atomics_in_system UAPI flag.
	 * Note that this also prohibits GPU atomics in LR mode for
	 * userptr and system memory on DGFX.
	 */
	return (!IS_DGFX(xe) || (!xe_vm_in_lr_mode(vm) ||
				 (bo && xe_bo_has_single_placement(bo))));
}

/**
 * xe_pt_stage_bind() - Build a disconnected page-table tree for a given address
 * range.
 * @tile: The tile we're building for.
 * @vma: The vma indicating the address range.
 * @range: The range indicating the address range.
 * @entries: Storage for the update entries used for connecting the tree to
 * the main tree at commit time.
 * @num_entries: On output contains the number of @entries used.
 * @clear_pt: Clear the page table entries.
 *
 * This function builds a disconnected page-table tree for a given address
 * range. The tree is connected to the main vm tree for the gpu using
 * xe_migrate_update_pgtables() and for the cpu using xe_pt_commit_bind().
 * The function builds xe_vm_pgtable_update structures for already existing
 * shared page-tables, and non-existing shared and non-shared page-tables
 * are built and populated directly.
 *
 * Return 0 on success, negative error code on error.
 */
static int
xe_pt_stage_bind(struct xe_tile *tile, struct xe_vma *vma,
		 struct xe_svm_range *range,
		 struct xe_vm_pgtable_update *entries,
		 u32 *num_entries, bool clear_pt)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_bo *bo = xe_vma_bo(vma);
	struct xe_res_cursor curs;
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_pt_stage_bind_walk xe_walk = {
		.base = {
			.ops = &xe_pt_stage_bind_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
			.staging = true,
		},
		.vm = vm,
		.tile = tile,
		.curs = &curs,
		.va_curs_start = range ? range->base.itree.start :
			xe_vma_start(vma),
		.vma = vma,
		.wupd.entries = entries,
		.clear_pt = clear_pt,
	};
	struct xe_pt *pt = vm->pt_root[tile->id];
	int ret;

	if (range) {
		/* Move this entire thing to xe_svm.c? */
		xe_svm_notifier_lock(vm);
		if (!xe_svm_range_pages_valid(range)) {
			xe_svm_range_debug(range, "BIND PREPARE - RETRY");
			xe_svm_notifier_unlock(vm);
			return -EAGAIN;
		}
		if (xe_svm_range_has_dma_mapping(range)) {
			xe_res_first_dma(range->base.dma_addr, 0,
					 range->base.itree.last + 1 - range->base.itree.start,
					 &curs);
			xe_svm_range_debug(range, "BIND PREPARE - MIXED");
		} else {
			xe_assert(xe, false);
		}
		/*
		 * Note, when unlocking the resource cursor dma addresses may become
		 * stale, but the bind will be aborted anyway at commit time.
		 */
		xe_svm_notifier_unlock(vm);
	}

	xe_walk.needs_64K = (vm->flags & XE_VM_FLAG_64K);
	if (clear_pt)
		goto walk_pt;

	if (vma->gpuva.flags & XE_VMA_ATOMIC_PTE_BIT) {
		xe_walk.default_vram_pte = xe_atomic_for_vram(vm) ? XE_USM_PPGTT_PTE_AE : 0;
		xe_walk.default_system_pte = xe_atomic_for_system(vm, bo) ?
			XE_USM_PPGTT_PTE_AE : 0;
	}

	xe_walk.default_vram_pte |= XE_PPGTT_PTE_DM;
	xe_walk.dma_offset = bo ? vram_region_gpu_offset(bo->ttm.resource) : 0;
	if (!range)
		xe_bo_assert_held(bo);

	if (!xe_vma_is_null(vma) && !range) {
		if (xe_vma_is_userptr(vma))
			xe_res_first_sg(to_userptr_vma(vma)->userptr.sg, 0,
					xe_vma_size(vma), &curs);
		else if (xe_bo_is_vram(bo) || xe_bo_is_stolen(bo))
			xe_res_first(bo->ttm.resource, xe_vma_bo_offset(vma),
				     xe_vma_size(vma), &curs);
		else
			xe_res_first_sg(xe_bo_sg(bo), xe_vma_bo_offset(vma),
					xe_vma_size(vma), &curs);
	} else if (!range) {
		curs.size = xe_vma_size(vma);
	}

walk_pt:
	ret = xe_pt_walk_range(&pt->base, pt->level,
			       range ? range->base.itree.start : xe_vma_start(vma),
			       range ? range->base.itree.last + 1 : xe_vma_end(vma),
			       &xe_walk.base);

	*num_entries = xe_walk.wupd.num_used_entries;
	return ret;
}

/**
 * xe_pt_nonshared_offsets() - Determine the non-shared entry offsets of a
 * shared pagetable.
 * @addr: The start address within the non-shared pagetable.
 * @end: The end address within the non-shared pagetable.
 * @level: The level of the non-shared pagetable.
 * @walk: Walk info. The function adjusts the walk action.
 * @action: next action to perform (see enum page_walk_action)
 * @offset: Ignored on input, First non-shared entry on output.
 * @end_offset: Ignored on input, Last non-shared entry + 1 on output.
 *
 * A non-shared page-table has some entries that belong to the address range
 * and others that don't. This function determines the entries that belong
 * fully to the address range. Depending on level, some entries may
 * partially belong to the address range (that can't happen at level 0).
 * The function detects that and adjust those offsets to not include those
 * partial entries. Iff it does detect partial entries, we know that there must
 * be shared page tables also at lower levels, so it adjusts the walk action
 * accordingly.
 *
 * Return: true if there were non-shared entries, false otherwise.
 */
static bool xe_pt_nonshared_offsets(u64 addr, u64 end, unsigned int level,
				    struct xe_pt_walk *walk,
				    enum page_walk_action *action,
				    pgoff_t *offset, pgoff_t *end_offset)
{
	u64 size = 1ull << walk->shifts[level];

	*offset = xe_pt_offset(addr, level, walk);
	*end_offset = xe_pt_num_entries(addr, end, level, walk) + *offset;

	if (!level)
		return true;

	/*
	 * If addr or next are not size aligned, there are shared pts at lower
	 * level, so in that case traverse down the subtree
	 */
	*action = ACTION_CONTINUE;
	if (!IS_ALIGNED(addr, size)) {
		*action = ACTION_SUBTREE;
		(*offset)++;
	}

	if (!IS_ALIGNED(end, size)) {
		*action = ACTION_SUBTREE;
		(*end_offset)--;
	}

	return *end_offset > *offset;
}

struct xe_pt_zap_ptes_walk {
	/** @base: The walk base-class */
	struct xe_pt_walk base;

	/* Input parameters for the walk */
	/** @tile: The tile we're building for */
	struct xe_tile *tile;

	/* Output */
	/** @needs_invalidate: Whether we need to invalidate TLB*/
	bool needs_invalidate;
};

static int xe_pt_zap_ptes_entry(struct xe_ptw *parent, pgoff_t offset,
				unsigned int level, u64 addr, u64 next,
				struct xe_ptw **child,
				enum page_walk_action *action,
				struct xe_pt_walk *walk)
{
	struct xe_pt_zap_ptes_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	struct xe_pt *xe_child = container_of(*child, typeof(*xe_child), base);
	pgoff_t end_offset;

	XE_WARN_ON(!*child);
	XE_WARN_ON(!level);

	/*
	 * Note that we're called from an entry callback, and we're dealing
	 * with the child of that entry rather than the parent, so need to
	 * adjust level down.
	 */
	if (xe_pt_nonshared_offsets(addr, next, --level, walk, action, &offset,
				    &end_offset)) {
		xe_map_memset(tile_to_xe(xe_walk->tile), &xe_child->bo->vmap,
			      offset * sizeof(u64), 0,
			      (end_offset - offset) * sizeof(u64));
		xe_walk->needs_invalidate = true;
	}

	return 0;
}

static const struct xe_pt_walk_ops xe_pt_zap_ptes_ops = {
	.pt_entry = xe_pt_zap_ptes_entry,
};

/**
 * xe_pt_zap_ptes() - Zap (zero) gpu ptes of an address range
 * @tile: The tile we're zapping for.
 * @vma: GPU VMA detailing address range.
 *
 * Eviction and Userptr invalidation needs to be able to zap the
 * gpu ptes of a given address range in pagefaulting mode.
 * In order to be able to do that, that function needs access to the shared
 * page-table entrieaso it can either clear the leaf PTEs or
 * clear the pointers to lower-level page-tables. The caller is required
 * to hold the necessary locks to ensure neither the page-table connectivity
 * nor the page-table entries of the range is updated from under us.
 *
 * Return: Whether ptes were actually updated and a TLB invalidation is
 * required.
 */
bool xe_pt_zap_ptes(struct xe_tile *tile, struct xe_vma *vma)
{
	struct xe_pt_zap_ptes_walk xe_walk = {
		.base = {
			.ops = &xe_pt_zap_ptes_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.tile = tile,
	};
	struct xe_pt *pt = xe_vma_vm(vma)->pt_root[tile->id];
	u8 pt_mask = (vma->tile_present & ~vma->tile_invalidated);

	if (xe_vma_bo(vma))
		xe_bo_assert_held(xe_vma_bo(vma));
	else if (xe_vma_is_userptr(vma))
		lockdep_assert_held(&xe_vma_vm(vma)->userptr.notifier_lock);

	if (!(pt_mask & BIT(tile->id)))
		return false;

	(void)xe_pt_walk_shared(&pt->base, pt->level, xe_vma_start(vma),
				xe_vma_end(vma), &xe_walk.base);

	return xe_walk.needs_invalidate;
}

/**
 * xe_pt_zap_ptes_range() - Zap (zero) gpu ptes of a SVM range
 * @tile: The tile we're zapping for.
 * @vm: The VM we're zapping for.
 * @range: The SVM range we're zapping for.
 *
 * SVM invalidation needs to be able to zap the gpu ptes of a given address
 * range. In order to be able to do that, that function needs access to the
 * shared page-table entries so it can either clear the leaf PTEs or
 * clear the pointers to lower-level page-tables. The caller is required
 * to hold the SVM notifier lock.
 *
 * Return: Whether ptes were actually updated and a TLB invalidation is
 * required.
 */
bool xe_pt_zap_ptes_range(struct xe_tile *tile, struct xe_vm *vm,
			  struct xe_svm_range *range)
{
	struct xe_pt_zap_ptes_walk xe_walk = {
		.base = {
			.ops = &xe_pt_zap_ptes_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.tile = tile,
	};
	struct xe_pt *pt = vm->pt_root[tile->id];
	u8 pt_mask = (range->tile_present & ~range->tile_invalidated);

	xe_svm_assert_in_notifier(vm);

	if (!(pt_mask & BIT(tile->id)))
		return false;

	(void)xe_pt_walk_shared(&pt->base, pt->level, range->base.itree.start,
				range->base.itree.last + 1, &xe_walk.base);

	return xe_walk.needs_invalidate;
}

static void
xe_vm_populate_pgtable(struct xe_migrate_pt_update *pt_update, struct xe_tile *tile,
		       struct iosys_map *map, void *data,
		       u32 qword_ofs, u32 num_qwords,
		       const struct xe_vm_pgtable_update *update)
{
	struct xe_pt_entry *ptes = update->pt_entries;
	u64 *ptr = data;
	u32 i;

	for (i = 0; i < num_qwords; i++) {
		if (map)
			xe_map_wr(tile_to_xe(tile), map, (qword_ofs + i) *
				  sizeof(u64), u64, ptes[i].pte);
		else
			ptr[i] = ptes[i].pte;
	}
}

static void xe_pt_cancel_bind(struct xe_vma *vma,
			      struct xe_vm_pgtable_update *entries,
			      u32 num_entries)
{
	u32 i, j;

	for (i = 0; i < num_entries; i++) {
		struct xe_pt *pt = entries[i].pt;

		if (!pt)
			continue;

		if (pt->level) {
			for (j = 0; j < entries[i].qwords; j++)
				xe_pt_destroy(entries[i].pt_entries[j].pt,
					      xe_vma_vm(vma)->flags, NULL);
		}

		kfree(entries[i].pt_entries);
		entries[i].pt_entries = NULL;
		entries[i].qwords = 0;
	}
}

#define XE_INVALID_VMA	((struct xe_vma *)(0xdeaddeadull))

static void xe_pt_commit_prepare_locks_assert(struct xe_vma *vma)
{
	struct xe_vm *vm;

	if (vma == XE_INVALID_VMA)
		return;

	vm = xe_vma_vm(vma);
	lockdep_assert_held(&vm->lock);

	if (!xe_vma_has_no_bo(vma))
		dma_resv_assert_held(xe_vma_bo(vma)->ttm.base.resv);

	xe_vm_assert_held(vm);
}

static void xe_pt_commit_locks_assert(struct xe_vma *vma)
{
	struct xe_vm *vm;

	if (vma == XE_INVALID_VMA)
		return;

	vm = xe_vma_vm(vma);
	xe_pt_commit_prepare_locks_assert(vma);

	if (xe_vma_is_userptr(vma))
		lockdep_assert_held_read(&vm->userptr.notifier_lock);
}

static void xe_pt_commit(struct xe_vma *vma,
			 struct xe_vm_pgtable_update *entries,
			 u32 num_entries, struct llist_head *deferred)
{
	u32 i, j;

	xe_pt_commit_locks_assert(vma);

	for (i = 0; i < num_entries; i++) {
		struct xe_pt *pt = entries[i].pt;
		struct xe_pt_dir *pt_dir;

		if (!pt->level)
			continue;

		pt_dir = as_xe_pt_dir(pt);
		for (j = 0; j < entries[i].qwords; j++) {
			struct xe_pt *oldpte = entries[i].pt_entries[j].pt;
			int j_ = j + entries[i].ofs;

			pt_dir->children[j_] = pt_dir->staging[j_];
			xe_pt_destroy(oldpte, (vma == XE_INVALID_VMA) ? 0 :
				      xe_vma_vm(vma)->flags, deferred);
		}
	}
}

static void xe_pt_abort_bind(struct xe_vma *vma,
			     struct xe_vm_pgtable_update *entries,
			     u32 num_entries, bool rebind)
{
	int i, j;

	xe_pt_commit_prepare_locks_assert(vma);

	for (i = num_entries - 1; i >= 0; --i) {
		struct xe_pt *pt = entries[i].pt;
		struct xe_pt_dir *pt_dir;

		if (!rebind)
			pt->num_live -= entries[i].qwords;

		if (!pt->level)
			continue;

		pt_dir = as_xe_pt_dir(pt);
		for (j = 0; j < entries[i].qwords; j++) {
			u32 j_ = j + entries[i].ofs;
			struct xe_pt *newpte = xe_pt_entry_staging(pt_dir, j_);
			struct xe_pt *oldpte = entries[i].pt_entries[j].pt;

			pt_dir->staging[j_] = oldpte ? &oldpte->base : 0;
			xe_pt_destroy(newpte, xe_vma_vm(vma)->flags, NULL);
		}
	}
}

static void xe_pt_commit_prepare_bind(struct xe_vma *vma,
				      struct xe_vm_pgtable_update *entries,
				      u32 num_entries, bool rebind)
{
	u32 i, j;

	xe_pt_commit_prepare_locks_assert(vma);

	for (i = 0; i < num_entries; i++) {
		struct xe_pt *pt = entries[i].pt;
		struct xe_pt_dir *pt_dir;

		if (!rebind)
			pt->num_live += entries[i].qwords;

		if (!pt->level)
			continue;

		pt_dir = as_xe_pt_dir(pt);
		for (j = 0; j < entries[i].qwords; j++) {
			u32 j_ = j + entries[i].ofs;
			struct xe_pt *newpte = entries[i].pt_entries[j].pt;
			struct xe_pt *oldpte = NULL;

			if (xe_pt_entry_staging(pt_dir, j_))
				oldpte = xe_pt_entry_staging(pt_dir, j_);

			pt_dir->staging[j_] = &newpte->base;
			entries[i].pt_entries[j].pt = oldpte;
		}
	}
}

static void xe_pt_free_bind(struct xe_vm_pgtable_update *entries,
			    u32 num_entries)
{
	u32 i;

	for (i = 0; i < num_entries; i++)
		kfree(entries[i].pt_entries);
}

static int
xe_pt_prepare_bind(struct xe_tile *tile, struct xe_vma *vma,
		   struct xe_svm_range *range,
		   struct xe_vm_pgtable_update *entries,
		   u32 *num_entries, bool invalidate_on_bind)
{
	int err;

	*num_entries = 0;
	err = xe_pt_stage_bind(tile, vma, range, entries, num_entries,
			       invalidate_on_bind);
	if (!err)
		xe_tile_assert(tile, *num_entries);

	return err;
}

static void xe_vm_dbg_print_entries(struct xe_device *xe,
				    const struct xe_vm_pgtable_update *entries,
				    unsigned int num_entries, bool bind)
#if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM))
{
	unsigned int i;

	vm_dbg(&xe->drm, "%s: %u entries to update\n", bind ? "bind" : "unbind",
	       num_entries);
	for (i = 0; i < num_entries; i++) {
		const struct xe_vm_pgtable_update *entry = &entries[i];
		struct xe_pt *xe_pt = entry->pt;
		u64 page_size = 1ull << xe_pt_shift(xe_pt->level);
		u64 end;
		u64 start;

		xe_assert(xe, !entry->pt->is_compact);
		start = entry->ofs * page_size;
		end = start + page_size * entry->qwords;
		vm_dbg(&xe->drm,
		       "\t%u: Update level %u at (%u + %u) [%llx...%llx) f:%x\n",
		       i, xe_pt->level, entry->ofs, entry->qwords,
		       xe_pt_addr(xe_pt) + start, xe_pt_addr(xe_pt) + end, 0);
	}
}
#else
{}
#endif

static bool no_in_syncs(struct xe_sync_entry *syncs, u32 num_syncs)
{
	int i;

	for (i = 0; i < num_syncs; i++) {
		struct dma_fence *fence = syncs[i].fence;

		if (fence && !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				       &fence->flags))
			return false;
	}

	return true;
}

static int job_test_add_deps(struct xe_sched_job *job,
			     struct dma_resv *resv,
			     enum dma_resv_usage usage)
{
	if (!job) {
		if (!dma_resv_test_signaled(resv, usage))
			return -ETIME;

		return 0;
	}

	return xe_sched_job_add_deps(job, resv, usage);
}

static int vma_add_deps(struct xe_vma *vma, struct xe_sched_job *job)
{
	struct xe_bo *bo = xe_vma_bo(vma);

	xe_bo_assert_held(bo);

	if (bo && !bo->vm)
		return job_test_add_deps(job, bo->ttm.base.resv,
					 DMA_RESV_USAGE_KERNEL);

	return 0;
}

static int op_add_deps(struct xe_vm *vm, struct xe_vma_op *op,
		       struct xe_sched_job *job)
{
	int err = 0;

	/*
	 * No need to check for is_cpu_addr_mirror here as vma_add_deps is a
	 * NOP if VMA is_cpu_addr_mirror
	 */

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if (!op->map.immediate && xe_vm_in_fault_mode(vm))
			break;

		err = vma_add_deps(op->map.vma, job);
		break;
	case DRM_GPUVA_OP_REMAP:
		if (op->remap.prev)
			err = vma_add_deps(op->remap.prev, job);
		if (!err && op->remap.next)
			err = vma_add_deps(op->remap.next, job);
		break;
	case DRM_GPUVA_OP_UNMAP:
		break;
	case DRM_GPUVA_OP_PREFETCH:
		err = vma_add_deps(gpuva_to_vma(op->base.prefetch.va), job);
		break;
	case DRM_GPUVA_OP_DRIVER:
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}

static int xe_pt_vm_dependencies(struct xe_sched_job *job,
				 struct xe_vm *vm,
				 struct xe_vma_ops *vops,
				 struct xe_vm_pgtable_update_ops *pt_update_ops,
				 struct xe_range_fence_tree *rftree)
{
	struct xe_range_fence *rtfence;
	struct dma_fence *fence;
	struct xe_vma_op *op;
	int err = 0, i;

	xe_vm_assert_held(vm);

	if (!job && !no_in_syncs(vops->syncs, vops->num_syncs))
		return -ETIME;

	if (!job && !xe_exec_queue_is_idle(pt_update_ops->q))
		return -ETIME;

	if (pt_update_ops->wait_vm_bookkeep || pt_update_ops->wait_vm_kernel) {
		err = job_test_add_deps(job, xe_vm_resv(vm),
					pt_update_ops->wait_vm_bookkeep ?
					DMA_RESV_USAGE_BOOKKEEP :
					DMA_RESV_USAGE_KERNEL);
		if (err)
			return err;
	}

	rtfence = xe_range_fence_tree_first(rftree, pt_update_ops->start,
					    pt_update_ops->last);
	while (rtfence) {
		fence = rtfence->fence;

		if (!dma_fence_is_signaled(fence)) {
			/*
			 * Is this a CPU update? GPU is busy updating, so return
			 * an error
			 */
			if (!job)
				return -ETIME;

			dma_fence_get(fence);
			err = drm_sched_job_add_dependency(&job->drm, fence);
			if (err)
				return err;
		}

		rtfence = xe_range_fence_tree_next(rtfence,
						   pt_update_ops->start,
						   pt_update_ops->last);
	}

	list_for_each_entry(op, &vops->list, link) {
		err = op_add_deps(vm, op, job);
		if (err)
			return err;
	}

	if (!(pt_update_ops->q->flags & EXEC_QUEUE_FLAG_KERNEL)) {
		if (job)
			err = xe_sched_job_last_fence_add_dep(job, vm);
		else
			err = xe_exec_queue_last_fence_test_dep(pt_update_ops->q, vm);
	}

	for (i = 0; job && !err && i < vops->num_syncs; i++)
		err = xe_sync_entry_add_deps(&vops->syncs[i], job);

	return err;
}

static int xe_pt_pre_commit(struct xe_migrate_pt_update *pt_update)
{
	struct xe_vma_ops *vops = pt_update->vops;
	struct xe_vm *vm = vops->vm;
	struct xe_range_fence_tree *rftree = &vm->rftree[pt_update->tile_id];
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&vops->pt_update_ops[pt_update->tile_id];

	return xe_pt_vm_dependencies(pt_update->job, vm, pt_update->vops,
				     pt_update_ops, rftree);
}

#ifdef CONFIG_DRM_XE_USERPTR_INVAL_INJECT

static bool xe_pt_userptr_inject_eagain(struct xe_userptr_vma *uvma)
{
	u32 divisor = uvma->userptr.divisor ? uvma->userptr.divisor : 2;
	static u32 count;

	if (count++ % divisor == divisor - 1) {
		uvma->userptr.divisor = divisor << 1;
		return true;
	}

	return false;
}

#else

static bool xe_pt_userptr_inject_eagain(struct xe_userptr_vma *uvma)
{
	return false;
}

#endif

static int vma_check_userptr(struct xe_vm *vm, struct xe_vma *vma,
			     struct xe_vm_pgtable_update_ops *pt_update)
{
	struct xe_userptr_vma *uvma;
	unsigned long notifier_seq;

	lockdep_assert_held_read(&vm->userptr.notifier_lock);

	if (!xe_vma_is_userptr(vma))
		return 0;

	uvma = to_userptr_vma(vma);
	if (xe_pt_userptr_inject_eagain(uvma))
		xe_vma_userptr_force_invalidate(uvma);

	notifier_seq = uvma->userptr.notifier_seq;

	if (!mmu_interval_read_retry(&uvma->userptr.notifier,
				     notifier_seq))
		return 0;

	if (xe_vm_in_fault_mode(vm))
		return -EAGAIN;

	/*
	 * Just continue the operation since exec or rebind worker
	 * will take care of rebinding.
	 */
	return 0;
}

static int op_check_userptr(struct xe_vm *vm, struct xe_vma_op *op,
			    struct xe_vm_pgtable_update_ops *pt_update)
{
	int err = 0;

	lockdep_assert_held_read(&vm->userptr.notifier_lock);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if (!op->map.immediate && xe_vm_in_fault_mode(vm))
			break;

		err = vma_check_userptr(vm, op->map.vma, pt_update);
		break;
	case DRM_GPUVA_OP_REMAP:
		if (op->remap.prev)
			err = vma_check_userptr(vm, op->remap.prev, pt_update);
		if (!err && op->remap.next)
			err = vma_check_userptr(vm, op->remap.next, pt_update);
		break;
	case DRM_GPUVA_OP_UNMAP:
		break;
	case DRM_GPUVA_OP_PREFETCH:
		err = vma_check_userptr(vm, gpuva_to_vma(op->base.prefetch.va),
					pt_update);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}

static int xe_pt_userptr_pre_commit(struct xe_migrate_pt_update *pt_update)
{
	struct xe_vm *vm = pt_update->vops->vm;
	struct xe_vma_ops *vops = pt_update->vops;
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&vops->pt_update_ops[pt_update->tile_id];
	struct xe_vma_op *op;
	int err;

	err = xe_pt_pre_commit(pt_update);
	if (err)
		return err;

	down_read(&vm->userptr.notifier_lock);

	list_for_each_entry(op, &vops->list, link) {
		err = op_check_userptr(vm, op, pt_update_ops);
		if (err) {
			up_read(&vm->userptr.notifier_lock);
			break;
		}
	}

	return err;
}

#if IS_ENABLED(CONFIG_DRM_XE_GPUSVM)
static int xe_pt_svm_pre_commit(struct xe_migrate_pt_update *pt_update)
{
	struct xe_vm *vm = pt_update->vops->vm;
	struct xe_vma_ops *vops = pt_update->vops;
	struct xe_vma_op *op;
	unsigned long i;
	int err;

	err = xe_pt_pre_commit(pt_update);
	if (err)
		return err;

	xe_svm_notifier_lock(vm);

	list_for_each_entry(op, &vops->list, link) {
		struct xe_svm_range *range = NULL;

		if (op->subop == XE_VMA_SUBOP_UNMAP_RANGE)
			continue;

		if (op->base.op == DRM_GPUVA_OP_PREFETCH) {
			xe_assert(vm->xe,
				  xe_vma_is_cpu_addr_mirror(gpuva_to_vma(op->base.prefetch.va)));
			xa_for_each(&op->prefetch_range.range, i, range) {
				xe_svm_range_debug(range, "PRE-COMMIT");

				if (!xe_svm_range_pages_valid(range)) {
					xe_svm_range_debug(range, "PRE-COMMIT - RETRY");
					xe_svm_notifier_unlock(vm);
					return -ENODATA;
				}
			}
		} else {
			xe_assert(vm->xe, xe_vma_is_cpu_addr_mirror(op->map_range.vma));
			xe_assert(vm->xe, op->subop == XE_VMA_SUBOP_MAP_RANGE);
			range = op->map_range.range;

			xe_svm_range_debug(range, "PRE-COMMIT");

			if (!xe_svm_range_pages_valid(range)) {
				xe_svm_range_debug(range, "PRE-COMMIT - RETRY");
				xe_svm_notifier_unlock(vm);
				return -EAGAIN;
			}
		}
	}

	return 0;
}
#endif

struct invalidation_fence {
	struct xe_gt_tlb_invalidation_fence base;
	struct xe_gt *gt;
	struct dma_fence *fence;
	struct dma_fence_cb cb;
	struct work_struct work;
	u64 start;
	u64 end;
	u32 asid;
};

static void invalidation_fence_cb(struct dma_fence *fence,
				  struct dma_fence_cb *cb)
{
	struct invalidation_fence *ifence =
		container_of(cb, struct invalidation_fence, cb);
	struct xe_device *xe = gt_to_xe(ifence->gt);

	trace_xe_gt_tlb_invalidation_fence_cb(xe, &ifence->base);
	if (!ifence->fence->error) {
		queue_work(system_wq, &ifence->work);
	} else {
		ifence->base.base.error = ifence->fence->error;
		xe_gt_tlb_invalidation_fence_signal(&ifence->base);
	}
	dma_fence_put(ifence->fence);
}

static void invalidation_fence_work_func(struct work_struct *w)
{
	struct invalidation_fence *ifence =
		container_of(w, struct invalidation_fence, work);
	struct xe_device *xe = gt_to_xe(ifence->gt);

	trace_xe_gt_tlb_invalidation_fence_work_func(xe, &ifence->base);
	xe_gt_tlb_invalidation_range(ifence->gt, &ifence->base, ifence->start,
				     ifence->end, ifence->asid);
}

static void invalidation_fence_init(struct xe_gt *gt,
				    struct invalidation_fence *ifence,
				    struct dma_fence *fence,
				    u64 start, u64 end, u32 asid)
{
	int ret;

	trace_xe_gt_tlb_invalidation_fence_create(gt_to_xe(gt), &ifence->base);

	xe_gt_tlb_invalidation_fence_init(gt, &ifence->base, false);

	ifence->fence = fence;
	ifence->gt = gt;
	ifence->start = start;
	ifence->end = end;
	ifence->asid = asid;

	INIT_WORK(&ifence->work, invalidation_fence_work_func);
	ret = dma_fence_add_callback(fence, &ifence->cb, invalidation_fence_cb);
	if (ret == -ENOENT) {
		dma_fence_put(ifence->fence);	/* Usually dropped in CB */
		invalidation_fence_work_func(&ifence->work);
	} else if (ret) {
		dma_fence_put(&ifence->base.base);	/* Caller ref */
		dma_fence_put(&ifence->base.base);	/* Creation ref */
	}

	xe_gt_assert(gt, !ret || ret == -ENOENT);
}

struct xe_pt_stage_unbind_walk {
	/** @base: The pagewalk base-class. */
	struct xe_pt_walk base;

	/* Input parameters for the walk */
	/** @tile: The tile we're unbinding from. */
	struct xe_tile *tile;

	/**
	 * @modified_start: Walk range start, modified to include any
	 * shared pagetables that we're the only user of and can thus
	 * treat as private.
	 */
	u64 modified_start;
	/** @modified_end: Walk range start, modified like @modified_start. */
	u64 modified_end;

	/* Output */
	/* @wupd: Structure to track the page-table updates we're building */
	struct xe_walk_update wupd;
};

/*
 * Check whether this range is the only one populating this pagetable,
 * and in that case, update the walk range checks so that higher levels don't
 * view us as a shared pagetable.
 */
static bool xe_pt_check_kill(u64 addr, u64 next, unsigned int level,
			     const struct xe_pt *child,
			     enum page_walk_action *action,
			     struct xe_pt_walk *walk)
{
	struct xe_pt_stage_unbind_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	unsigned int shift = walk->shifts[level];
	u64 size = 1ull << shift;

	if (IS_ALIGNED(addr, size) && IS_ALIGNED(next, size) &&
	    ((next - addr) >> shift) == child->num_live) {
		u64 size = 1ull << walk->shifts[level + 1];

		*action = ACTION_CONTINUE;

		if (xe_walk->modified_start >= addr)
			xe_walk->modified_start = round_down(addr, size);
		if (xe_walk->modified_end <= next)
			xe_walk->modified_end = round_up(next, size);

		return true;
	}

	return false;
}

static int xe_pt_stage_unbind_entry(struct xe_ptw *parent, pgoff_t offset,
				    unsigned int level, u64 addr, u64 next,
				    struct xe_ptw **child,
				    enum page_walk_action *action,
				    struct xe_pt_walk *walk)
{
	struct xe_pt *xe_child = container_of(*child, typeof(*xe_child), base);

	XE_WARN_ON(!*child);
	XE_WARN_ON(!level);

	xe_pt_check_kill(addr, next, level - 1, xe_child, action, walk);

	return 0;
}

static int
xe_pt_stage_unbind_post_descend(struct xe_ptw *parent, pgoff_t offset,
				unsigned int level, u64 addr, u64 next,
				struct xe_ptw **child,
				enum page_walk_action *action,
				struct xe_pt_walk *walk)
{
	struct xe_pt_stage_unbind_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	struct xe_pt *xe_child = container_of(*child, typeof(*xe_child), base);
	pgoff_t end_offset;
	u64 size = 1ull << walk->shifts[--level];
	int err;

	if (!IS_ALIGNED(addr, size))
		addr = xe_walk->modified_start;
	if (!IS_ALIGNED(next, size))
		next = xe_walk->modified_end;

	/* Parent == *child is the root pt. Don't kill it. */
	if (parent != *child &&
	    xe_pt_check_kill(addr, next, level, xe_child, action, walk))
		return 0;

	if (!xe_pt_nonshared_offsets(addr, next, level, walk, action, &offset,
				     &end_offset))
		return 0;

	err = xe_pt_new_shared(&xe_walk->wupd, xe_child, offset, true);
	if (err)
		return err;

	xe_walk->wupd.updates[level].update->qwords = end_offset - offset;

	return 0;
}

static const struct xe_pt_walk_ops xe_pt_stage_unbind_ops = {
	.pt_entry = xe_pt_stage_unbind_entry,
	.pt_post_descend = xe_pt_stage_unbind_post_descend,
};

/**
 * xe_pt_stage_unbind() - Build page-table update structures for an unbind
 * operation
 * @tile: The tile we're unbinding for.
 * @vm: The vm
 * @vma: The vma we're unbinding.
 * @range: The range we're unbinding.
 * @entries: Caller-provided storage for the update structures.
 *
 * Builds page-table update structures for an unbind operation. The function
 * will attempt to remove all page-tables that we're the only user
 * of, and for that to work, the unbind operation must be committed in the
 * same critical section that blocks racing binds to the same page-table tree.
 *
 * Return: The number of entries used.
 */
static unsigned int xe_pt_stage_unbind(struct xe_tile *tile,
				       struct xe_vm *vm,
				       struct xe_vma *vma,
				       struct xe_svm_range *range,
				       struct xe_vm_pgtable_update *entries)
{
	u64 start = range ? range->base.itree.start : xe_vma_start(vma);
	u64 end = range ? range->base.itree.last + 1 : xe_vma_end(vma);
	struct xe_pt_stage_unbind_walk xe_walk = {
		.base = {
			.ops = &xe_pt_stage_unbind_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
			.staging = true,
		},
		.tile = tile,
		.modified_start = start,
		.modified_end = end,
		.wupd.entries = entries,
	};
	struct xe_pt *pt = vm->pt_root[tile->id];

	(void)xe_pt_walk_shared(&pt->base, pt->level, start, end,
				&xe_walk.base);

	return xe_walk.wupd.num_used_entries;
}

static void
xe_migrate_clear_pgtable_callback(struct xe_migrate_pt_update *pt_update,
				  struct xe_tile *tile, struct iosys_map *map,
				  void *ptr, u32 qword_ofs, u32 num_qwords,
				  const struct xe_vm_pgtable_update *update)
{
	struct xe_vm *vm = pt_update->vops->vm;
	u64 empty = __xe_pt_empty_pte(tile, vm, update->pt->level);
	int i;

	if (map && map->is_iomem)
		for (i = 0; i < num_qwords; ++i)
			xe_map_wr(tile_to_xe(tile), map, (qword_ofs + i) *
				  sizeof(u64), u64, empty);
	else if (map)
		memset64(map->vaddr + qword_ofs * sizeof(u64), empty,
			 num_qwords);
	else
		memset64(ptr, empty, num_qwords);
}

static void xe_pt_abort_unbind(struct xe_vma *vma,
			       struct xe_vm_pgtable_update *entries,
			       u32 num_entries)
{
	int i, j;

	xe_pt_commit_prepare_locks_assert(vma);

	for (i = num_entries - 1; i >= 0; --i) {
		struct xe_vm_pgtable_update *entry = &entries[i];
		struct xe_pt *pt = entry->pt;
		struct xe_pt_dir *pt_dir = as_xe_pt_dir(pt);

		pt->num_live += entry->qwords;

		if (!pt->level)
			continue;

		for (j = entry->ofs; j < entry->ofs + entry->qwords; j++)
			pt_dir->staging[j] =
				entries[i].pt_entries[j - entry->ofs].pt ?
				&entries[i].pt_entries[j - entry->ofs].pt->base : NULL;
	}
}

static void
xe_pt_commit_prepare_unbind(struct xe_vma *vma,
			    struct xe_vm_pgtable_update *entries,
			    u32 num_entries)
{
	int i, j;

	xe_pt_commit_prepare_locks_assert(vma);

	for (i = 0; i < num_entries; ++i) {
		struct xe_vm_pgtable_update *entry = &entries[i];
		struct xe_pt *pt = entry->pt;
		struct xe_pt_dir *pt_dir;

		pt->num_live -= entry->qwords;
		if (!pt->level)
			continue;

		pt_dir = as_xe_pt_dir(pt);
		for (j = entry->ofs; j < entry->ofs + entry->qwords; j++) {
			entry->pt_entries[j - entry->ofs].pt =
				xe_pt_entry_staging(pt_dir, j);
			pt_dir->staging[j] = NULL;
		}
	}
}

static void
xe_pt_update_ops_rfence_interval(struct xe_vm_pgtable_update_ops *pt_update_ops,
				 u64 start, u64 end)
{
	u64 last;
	u32 current_op = pt_update_ops->current_op;
	struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[current_op];
	int i, level = 0;

	for (i = 0; i < pt_op->num_entries; i++) {
		const struct xe_vm_pgtable_update *entry = &pt_op->entries[i];

		if (entry->pt->level > level)
			level = entry->pt->level;
	}

	/* Greedy (non-optimal) calculation but simple */
	start = ALIGN_DOWN(start, 0x1ull << xe_pt_shift(level));
	last = ALIGN(end, 0x1ull << xe_pt_shift(level)) - 1;

	if (start < pt_update_ops->start)
		pt_update_ops->start = start;
	if (last > pt_update_ops->last)
		pt_update_ops->last = last;
}

static int vma_reserve_fences(struct xe_device *xe, struct xe_vma *vma)
{
	int shift = xe_device_get_root_tile(xe)->media_gt ? 1 : 0;

	if (!xe_vma_has_no_bo(vma) && !xe_vma_bo(vma)->vm)
		return dma_resv_reserve_fences(xe_vma_bo(vma)->ttm.base.resv,
					       xe->info.tile_count << shift);

	return 0;
}

static int bind_op_prepare(struct xe_vm *vm, struct xe_tile *tile,
			   struct xe_vm_pgtable_update_ops *pt_update_ops,
			   struct xe_vma *vma, bool invalidate_on_bind)
{
	u32 current_op = pt_update_ops->current_op;
	struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[current_op];
	int err;

	xe_tile_assert(tile, !xe_vma_is_cpu_addr_mirror(vma));
	xe_bo_assert_held(xe_vma_bo(vma));

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "Preparing bind, with range [%llx...%llx)\n",
	       xe_vma_start(vma), xe_vma_end(vma) - 1);

	pt_op->vma = NULL;
	pt_op->bind = true;
	pt_op->rebind = BIT(tile->id) & vma->tile_present;

	err = vma_reserve_fences(tile_to_xe(tile), vma);
	if (err)
		return err;

	err = xe_pt_prepare_bind(tile, vma, NULL, pt_op->entries,
				 &pt_op->num_entries, invalidate_on_bind);
	if (!err) {
		xe_tile_assert(tile, pt_op->num_entries <=
			       ARRAY_SIZE(pt_op->entries));
		xe_vm_dbg_print_entries(tile_to_xe(tile), pt_op->entries,
					pt_op->num_entries, true);

		xe_pt_update_ops_rfence_interval(pt_update_ops,
						 xe_vma_start(vma),
						 xe_vma_end(vma));
		++pt_update_ops->current_op;
		pt_update_ops->needs_userptr_lock |= xe_vma_is_userptr(vma);

		/*
		 * If rebind, we have to invalidate TLB on !LR vms to invalidate
		 * cached PTEs point to freed memory. On LR vms this is done
		 * automatically when the context is re-enabled by the rebind worker,
		 * or in fault mode it was invalidated on PTE zapping.
		 *
		 * If !rebind, and scratch enabled VMs, there is a chance the scratch
		 * PTE is already cached in the TLB so it needs to be invalidated.
		 * On !LR VMs this is done in the ring ops preceding a batch, but on
		 * LR, in particular on user-space batch buffer chaining, it needs to
		 * be done here.
		 */
		if ((!pt_op->rebind && xe_vm_has_scratch(vm) &&
		     xe_vm_in_lr_mode(vm)))
			pt_update_ops->needs_invalidation = true;
		else if (pt_op->rebind && !xe_vm_in_lr_mode(vm))
			/* We bump also if batch_invalidate_tlb is true */
			vm->tlb_flush_seqno++;

		vma->tile_staged |= BIT(tile->id);
		pt_op->vma = vma;
		xe_pt_commit_prepare_bind(vma, pt_op->entries,
					  pt_op->num_entries, pt_op->rebind);
	} else {
		xe_pt_cancel_bind(vma, pt_op->entries, pt_op->num_entries);
	}

	return err;
}

static int bind_range_prepare(struct xe_vm *vm, struct xe_tile *tile,
			      struct xe_vm_pgtable_update_ops *pt_update_ops,
			      struct xe_vma *vma, struct xe_svm_range *range)
{
	u32 current_op = pt_update_ops->current_op;
	struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[current_op];
	int err;

	xe_tile_assert(tile, xe_vma_is_cpu_addr_mirror(vma));

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "Preparing bind, with range [%lx...%lx)\n",
	       range->base.itree.start, range->base.itree.last);

	pt_op->vma = NULL;
	pt_op->bind = true;
	pt_op->rebind = BIT(tile->id) & range->tile_present;

	err = xe_pt_prepare_bind(tile, vma, range, pt_op->entries,
				 &pt_op->num_entries, false);
	if (!err) {
		xe_tile_assert(tile, pt_op->num_entries <=
			       ARRAY_SIZE(pt_op->entries));
		xe_vm_dbg_print_entries(tile_to_xe(tile), pt_op->entries,
					pt_op->num_entries, true);

		xe_pt_update_ops_rfence_interval(pt_update_ops,
						 range->base.itree.start,
						 range->base.itree.last + 1);
		++pt_update_ops->current_op;
		pt_update_ops->needs_svm_lock = true;

		pt_op->vma = vma;
		xe_pt_commit_prepare_bind(vma, pt_op->entries,
					  pt_op->num_entries, pt_op->rebind);
	} else {
		xe_pt_cancel_bind(vma, pt_op->entries, pt_op->num_entries);
	}

	return err;
}

static int unbind_op_prepare(struct xe_tile *tile,
			     struct xe_vm_pgtable_update_ops *pt_update_ops,
			     struct xe_vma *vma)
{
	u32 current_op = pt_update_ops->current_op;
	struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[current_op];
	int err;

	if (!((vma->tile_present | vma->tile_staged) & BIT(tile->id)))
		return 0;

	xe_tile_assert(tile, !xe_vma_is_cpu_addr_mirror(vma));
	xe_bo_assert_held(xe_vma_bo(vma));

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "Preparing unbind, with range [%llx...%llx)\n",
	       xe_vma_start(vma), xe_vma_end(vma) - 1);

	pt_op->vma = vma;
	pt_op->bind = false;
	pt_op->rebind = false;

	err = vma_reserve_fences(tile_to_xe(tile), vma);
	if (err)
		return err;

	pt_op->num_entries = xe_pt_stage_unbind(tile, xe_vma_vm(vma),
						vma, NULL, pt_op->entries);

	xe_vm_dbg_print_entries(tile_to_xe(tile), pt_op->entries,
				pt_op->num_entries, false);
	xe_pt_update_ops_rfence_interval(pt_update_ops, xe_vma_start(vma),
					 xe_vma_end(vma));
	++pt_update_ops->current_op;
	pt_update_ops->needs_userptr_lock |= xe_vma_is_userptr(vma);
	pt_update_ops->needs_invalidation = true;

	xe_pt_commit_prepare_unbind(vma, pt_op->entries, pt_op->num_entries);

	return 0;
}

static bool
xe_pt_op_check_range_skip_invalidation(struct xe_vm_pgtable_update_op *pt_op,
				       struct xe_svm_range *range)
{
	struct xe_vm_pgtable_update *update = pt_op->entries;

	XE_WARN_ON(!pt_op->num_entries);

	/*
	 * We can't skip the invalidation if we are removing PTEs that span more
	 * than the range, do some checks to ensure we are removing PTEs that
	 * are invalid.
	 */

	if (pt_op->num_entries > 1)
		return false;

	if (update->pt->level == 0)
		return true;

	if (update->pt->level == 1)
		return xe_svm_range_size(range) >= SZ_2M;

	return false;
}

static int unbind_range_prepare(struct xe_vm *vm,
				struct xe_tile *tile,
				struct xe_vm_pgtable_update_ops *pt_update_ops,
				struct xe_svm_range *range)
{
	u32 current_op = pt_update_ops->current_op;
	struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[current_op];

	if (!(range->tile_present & BIT(tile->id)))
		return 0;

	vm_dbg(&vm->xe->drm,
	       "Preparing unbind, with range [%lx...%lx)\n",
	       range->base.itree.start, range->base.itree.last);

	pt_op->vma = XE_INVALID_VMA;
	pt_op->bind = false;
	pt_op->rebind = false;

	pt_op->num_entries = xe_pt_stage_unbind(tile, vm, NULL, range,
						pt_op->entries);

	xe_vm_dbg_print_entries(tile_to_xe(tile), pt_op->entries,
				pt_op->num_entries, false);
	xe_pt_update_ops_rfence_interval(pt_update_ops, range->base.itree.start,
					 range->base.itree.last + 1);
	++pt_update_ops->current_op;
	pt_update_ops->needs_svm_lock = true;
	pt_update_ops->needs_invalidation |= xe_vm_has_scratch(vm) ||
		xe_vm_has_valid_gpu_mapping(tile, range->tile_present,
					    range->tile_invalidated) ||
		!xe_pt_op_check_range_skip_invalidation(pt_op, range);

	xe_pt_commit_prepare_unbind(XE_INVALID_VMA, pt_op->entries,
				    pt_op->num_entries);

	return 0;
}

static int op_prepare(struct xe_vm *vm,
		      struct xe_tile *tile,
		      struct xe_vm_pgtable_update_ops *pt_update_ops,
		      struct xe_vma_op *op)
{
	int err = 0;

	xe_vm_assert_held(vm);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if ((!op->map.immediate && xe_vm_in_fault_mode(vm) &&
		     !op->map.invalidate_on_bind) ||
		    op->map.is_cpu_addr_mirror)
			break;

		err = bind_op_prepare(vm, tile, pt_update_ops, op->map.vma,
				      op->map.invalidate_on_bind);
		pt_update_ops->wait_vm_kernel = true;
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		struct xe_vma *old = gpuva_to_vma(op->base.remap.unmap->va);

		if (xe_vma_is_cpu_addr_mirror(old))
			break;

		err = unbind_op_prepare(tile, pt_update_ops, old);

		if (!err && op->remap.prev) {
			err = bind_op_prepare(vm, tile, pt_update_ops,
					      op->remap.prev, false);
			pt_update_ops->wait_vm_bookkeep = true;
		}
		if (!err && op->remap.next) {
			err = bind_op_prepare(vm, tile, pt_update_ops,
					      op->remap.next, false);
			pt_update_ops->wait_vm_bookkeep = true;
		}
		break;
	}
	case DRM_GPUVA_OP_UNMAP:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.unmap.va);

		if (xe_vma_is_cpu_addr_mirror(vma))
			break;

		err = unbind_op_prepare(tile, pt_update_ops, vma);
		break;
	}
	case DRM_GPUVA_OP_PREFETCH:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.prefetch.va);

		if (xe_vma_is_cpu_addr_mirror(vma)) {
			struct xe_svm_range *range;
			unsigned long i;

			xa_for_each(&op->prefetch_range.range, i, range) {
				err = bind_range_prepare(vm, tile, pt_update_ops,
							 vma, range);
				if (err)
					return err;
			}
		} else {
			err = bind_op_prepare(vm, tile, pt_update_ops, vma, false);
			pt_update_ops->wait_vm_kernel = true;
		}
		break;
	}
	case DRM_GPUVA_OP_DRIVER:
		if (op->subop == XE_VMA_SUBOP_MAP_RANGE) {
			xe_assert(vm->xe, xe_vma_is_cpu_addr_mirror(op->map_range.vma));

			err = bind_range_prepare(vm, tile, pt_update_ops,
						 op->map_range.vma,
						 op->map_range.range);
		} else if (op->subop == XE_VMA_SUBOP_UNMAP_RANGE) {
			err = unbind_range_prepare(vm, tile, pt_update_ops,
						   op->unmap_range.range);
		}
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}

static void
xe_pt_update_ops_init(struct xe_vm_pgtable_update_ops *pt_update_ops)
{
	init_llist_head(&pt_update_ops->deferred);
	pt_update_ops->start = ~0x0ull;
	pt_update_ops->last = 0x0ull;
}

/**
 * xe_pt_update_ops_prepare() - Prepare PT update operations
 * @tile: Tile of PT update operations
 * @vops: VMA operationa
 *
 * Prepare PT update operations which includes updating internal PT state,
 * allocate memory for page tables, populate page table being pruned in, and
 * create PT update operations for leaf insertion / removal.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_pt_update_ops_prepare(struct xe_tile *tile, struct xe_vma_ops *vops)
{
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&vops->pt_update_ops[tile->id];
	struct xe_vma_op *op;
	int shift = tile->media_gt ? 1 : 0;
	int err;

	lockdep_assert_held(&vops->vm->lock);
	xe_vm_assert_held(vops->vm);

	xe_pt_update_ops_init(pt_update_ops);

	err = dma_resv_reserve_fences(xe_vm_resv(vops->vm),
				      tile_to_xe(tile)->info.tile_count << shift);
	if (err)
		return err;

	list_for_each_entry(op, &vops->list, link) {
		err = op_prepare(vops->vm, tile, pt_update_ops, op);

		if (err)
			return err;
	}

	xe_tile_assert(tile, pt_update_ops->current_op <=
		       pt_update_ops->num_ops);

#ifdef TEST_VM_OPS_ERROR
	if (vops->inject_error &&
	    vops->vm->xe->vm_inject_error_position == FORCE_OP_ERROR_PREPARE)
		return -ENOSPC;
#endif

	return 0;
}
ALLOW_ERROR_INJECTION(xe_pt_update_ops_prepare, ERRNO);

static void bind_op_commit(struct xe_vm *vm, struct xe_tile *tile,
			   struct xe_vm_pgtable_update_ops *pt_update_ops,
			   struct xe_vma *vma, struct dma_fence *fence,
			   struct dma_fence *fence2, bool invalidate_on_bind)
{
	xe_tile_assert(tile, !xe_vma_is_cpu_addr_mirror(vma));

	if (!xe_vma_has_no_bo(vma) && !xe_vma_bo(vma)->vm) {
		dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence,
				   pt_update_ops->wait_vm_bookkeep ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);
		if (fence2)
			dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence2,
					   pt_update_ops->wait_vm_bookkeep ?
					   DMA_RESV_USAGE_KERNEL :
					   DMA_RESV_USAGE_BOOKKEEP);
	}
	/* All WRITE_ONCE pair with READ_ONCE in xe_vm_has_valid_gpu_mapping() */
	WRITE_ONCE(vma->tile_present, vma->tile_present | BIT(tile->id));
	if (invalidate_on_bind)
		WRITE_ONCE(vma->tile_invalidated,
			   vma->tile_invalidated | BIT(tile->id));
	else
		WRITE_ONCE(vma->tile_invalidated,
			   vma->tile_invalidated & ~BIT(tile->id));
	vma->tile_staged &= ~BIT(tile->id);
	if (xe_vma_is_userptr(vma)) {
		lockdep_assert_held_read(&vm->userptr.notifier_lock);
		to_userptr_vma(vma)->userptr.initial_bind = true;
	}

	/*
	 * Kick rebind worker if this bind triggers preempt fences and not in
	 * the rebind worker
	 */
	if (pt_update_ops->wait_vm_bookkeep &&
	    xe_vm_in_preempt_fence_mode(vm) &&
	    !current->mm)
		xe_vm_queue_rebind_worker(vm);
}

static void unbind_op_commit(struct xe_vm *vm, struct xe_tile *tile,
			     struct xe_vm_pgtable_update_ops *pt_update_ops,
			     struct xe_vma *vma, struct dma_fence *fence,
			     struct dma_fence *fence2)
{
	xe_tile_assert(tile, !xe_vma_is_cpu_addr_mirror(vma));

	if (!xe_vma_has_no_bo(vma) && !xe_vma_bo(vma)->vm) {
		dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence,
				   pt_update_ops->wait_vm_bookkeep ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);
		if (fence2)
			dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence2,
					   pt_update_ops->wait_vm_bookkeep ?
					   DMA_RESV_USAGE_KERNEL :
					   DMA_RESV_USAGE_BOOKKEEP);
	}
	vma->tile_present &= ~BIT(tile->id);
	if (!vma->tile_present) {
		list_del_init(&vma->combined_links.rebind);
		if (xe_vma_is_userptr(vma)) {
			lockdep_assert_held_read(&vm->userptr.notifier_lock);

			spin_lock(&vm->userptr.invalidated_lock);
			list_del_init(&to_userptr_vma(vma)->userptr.invalidate_link);
			spin_unlock(&vm->userptr.invalidated_lock);
		}
	}
}

static void range_present_and_invalidated_tile(struct xe_vm *vm,
					       struct xe_svm_range *range,
					       u8 tile_id)
{
	/* All WRITE_ONCE pair with READ_ONCE in xe_vm_has_valid_gpu_mapping() */

	lockdep_assert_held(&vm->svm.gpusvm.notifier_lock);

	WRITE_ONCE(range->tile_present, range->tile_present | BIT(tile_id));
	WRITE_ONCE(range->tile_invalidated, range->tile_invalidated & ~BIT(tile_id));
}

static void op_commit(struct xe_vm *vm,
		      struct xe_tile *tile,
		      struct xe_vm_pgtable_update_ops *pt_update_ops,
		      struct xe_vma_op *op, struct dma_fence *fence,
		      struct dma_fence *fence2)
{
	xe_vm_assert_held(vm);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if ((!op->map.immediate && xe_vm_in_fault_mode(vm)) ||
		    op->map.is_cpu_addr_mirror)
			break;

		bind_op_commit(vm, tile, pt_update_ops, op->map.vma, fence,
			       fence2, op->map.invalidate_on_bind);
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		struct xe_vma *old = gpuva_to_vma(op->base.remap.unmap->va);

		if (xe_vma_is_cpu_addr_mirror(old))
			break;

		unbind_op_commit(vm, tile, pt_update_ops, old, fence, fence2);

		if (op->remap.prev)
			bind_op_commit(vm, tile, pt_update_ops, op->remap.prev,
				       fence, fence2, false);
		if (op->remap.next)
			bind_op_commit(vm, tile, pt_update_ops, op->remap.next,
				       fence, fence2, false);
		break;
	}
	case DRM_GPUVA_OP_UNMAP:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.unmap.va);

		if (!xe_vma_is_cpu_addr_mirror(vma))
			unbind_op_commit(vm, tile, pt_update_ops, vma, fence,
					 fence2);
		break;
	}
	case DRM_GPUVA_OP_PREFETCH:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.prefetch.va);

		if (xe_vma_is_cpu_addr_mirror(vma)) {
			struct xe_svm_range *range = NULL;
			unsigned long i;

			xa_for_each(&op->prefetch_range.range, i, range)
				range_present_and_invalidated_tile(vm, range, tile->id);
		} else {
			bind_op_commit(vm, tile, pt_update_ops, vma, fence,
				       fence2, false);
		}
		break;
	}
	case DRM_GPUVA_OP_DRIVER:
	{
		/* WRITE_ONCE pairs with READ_ONCE in xe_vm_has_valid_gpu_mapping() */
		if (op->subop == XE_VMA_SUBOP_MAP_RANGE)
			range_present_and_invalidated_tile(vm, op->map_range.range, tile->id);
		else if (op->subop == XE_VMA_SUBOP_UNMAP_RANGE)
			WRITE_ONCE(op->unmap_range.range->tile_present,
				   op->unmap_range.range->tile_present &
				   ~BIT(tile->id));

		break;
	}
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}
}

static const struct xe_migrate_pt_update_ops migrate_ops = {
	.populate = xe_vm_populate_pgtable,
	.clear = xe_migrate_clear_pgtable_callback,
	.pre_commit = xe_pt_pre_commit,
};

static const struct xe_migrate_pt_update_ops userptr_migrate_ops = {
	.populate = xe_vm_populate_pgtable,
	.clear = xe_migrate_clear_pgtable_callback,
	.pre_commit = xe_pt_userptr_pre_commit,
};

#if IS_ENABLED(CONFIG_DRM_XE_GPUSVM)
static const struct xe_migrate_pt_update_ops svm_migrate_ops = {
	.populate = xe_vm_populate_pgtable,
	.clear = xe_migrate_clear_pgtable_callback,
	.pre_commit = xe_pt_svm_pre_commit,
};
#else
static const struct xe_migrate_pt_update_ops svm_migrate_ops;
#endif

/**
 * xe_pt_update_ops_run() - Run PT update operations
 * @tile: Tile of PT update operations
 * @vops: VMA operationa
 *
 * Run PT update operations which includes committing internal PT state changes,
 * creating job for PT update operations for leaf insertion / removal, and
 * installing job fence in various places.
 *
 * Return: fence on success, negative ERR_PTR on error.
 */
struct dma_fence *
xe_pt_update_ops_run(struct xe_tile *tile, struct xe_vma_ops *vops)
{
	struct xe_vm *vm = vops->vm;
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&vops->pt_update_ops[tile->id];
	struct dma_fence *fence;
	struct invalidation_fence *ifence = NULL, *mfence = NULL;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct xe_range_fence *rfence;
	struct xe_vma_op *op;
	int err = 0, i;
	struct xe_migrate_pt_update update = {
		.ops = pt_update_ops->needs_svm_lock ?
			&svm_migrate_ops :
			pt_update_ops->needs_userptr_lock ?
			&userptr_migrate_ops :
			&migrate_ops,
		.vops = vops,
		.tile_id = tile->id,
	};

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	if (!pt_update_ops->current_op) {
		xe_tile_assert(tile, xe_vm_in_fault_mode(vm));

		return dma_fence_get_stub();
	}

#ifdef TEST_VM_OPS_ERROR
	if (vops->inject_error &&
	    vm->xe->vm_inject_error_position == FORCE_OP_ERROR_RUN)
		return ERR_PTR(-ENOSPC);
#endif

	if (pt_update_ops->needs_invalidation) {
		ifence = kzalloc(sizeof(*ifence), GFP_KERNEL);
		if (!ifence) {
			err = -ENOMEM;
			goto kill_vm_tile1;
		}
		if (tile->media_gt) {
			mfence = kzalloc(sizeof(*ifence), GFP_KERNEL);
			if (!mfence) {
				err = -ENOMEM;
				goto free_ifence;
			}
			fences = kmalloc_array(2, sizeof(*fences), GFP_KERNEL);
			if (!fences) {
				err = -ENOMEM;
				goto free_ifence;
			}
			cf = dma_fence_array_alloc(2);
			if (!cf) {
				err = -ENOMEM;
				goto free_ifence;
			}
		}
	}

	rfence = kzalloc(sizeof(*rfence), GFP_KERNEL);
	if (!rfence) {
		err = -ENOMEM;
		goto free_ifence;
	}

	fence = xe_migrate_update_pgtables(tile->migrate, &update);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
		goto free_rfence;
	}

	/* Point of no return - VM killed if failure after this */
	for (i = 0; i < pt_update_ops->current_op; ++i) {
		struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[i];

		xe_pt_commit(pt_op->vma, pt_op->entries,
			     pt_op->num_entries, &pt_update_ops->deferred);
		pt_op->vma = NULL;	/* skip in xe_pt_update_ops_abort */
	}

	if (xe_range_fence_insert(&vm->rftree[tile->id], rfence,
				  &xe_range_fence_kfree_ops,
				  pt_update_ops->start,
				  pt_update_ops->last, fence))
		dma_fence_wait(fence, false);

	/* tlb invalidation must be done before signaling rebind */
	if (ifence) {
		if (mfence)
			dma_fence_get(fence);
		invalidation_fence_init(tile->primary_gt, ifence, fence,
					pt_update_ops->start,
					pt_update_ops->last, vm->usm.asid);
		if (mfence) {
			invalidation_fence_init(tile->media_gt, mfence, fence,
						pt_update_ops->start,
						pt_update_ops->last, vm->usm.asid);
			fences[0] = &ifence->base.base;
			fences[1] = &mfence->base.base;
			dma_fence_array_init(cf, 2, fences,
					     vm->composite_fence_ctx,
					     vm->composite_fence_seqno++,
					     false);
			fence = &cf->base;
		} else {
			fence = &ifence->base.base;
		}
	}

	if (!mfence) {
		dma_resv_add_fence(xe_vm_resv(vm), fence,
				   pt_update_ops->wait_vm_bookkeep ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);

		list_for_each_entry(op, &vops->list, link)
			op_commit(vops->vm, tile, pt_update_ops, op, fence, NULL);
	} else {
		dma_resv_add_fence(xe_vm_resv(vm), &ifence->base.base,
				   pt_update_ops->wait_vm_bookkeep ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);

		dma_resv_add_fence(xe_vm_resv(vm), &mfence->base.base,
				   pt_update_ops->wait_vm_bookkeep ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);

		list_for_each_entry(op, &vops->list, link)
			op_commit(vops->vm, tile, pt_update_ops, op,
				  &ifence->base.base, &mfence->base.base);
	}

	if (pt_update_ops->needs_svm_lock)
		xe_svm_notifier_unlock(vm);
	if (pt_update_ops->needs_userptr_lock)
		up_read(&vm->userptr.notifier_lock);

	return fence;

free_rfence:
	kfree(rfence);
free_ifence:
	kfree(cf);
	kfree(fences);
	kfree(mfence);
	kfree(ifence);
kill_vm_tile1:
	if (err != -EAGAIN && err != -ENODATA && tile->id)
		xe_vm_kill(vops->vm, false);

	return ERR_PTR(err);
}
ALLOW_ERROR_INJECTION(xe_pt_update_ops_run, ERRNO);

/**
 * xe_pt_update_ops_fini() - Finish PT update operations
 * @tile: Tile of PT update operations
 * @vops: VMA operations
 *
 * Finish PT update operations by committing to destroy page table memory
 */
void xe_pt_update_ops_fini(struct xe_tile *tile, struct xe_vma_ops *vops)
{
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&vops->pt_update_ops[tile->id];
	int i;

	lockdep_assert_held(&vops->vm->lock);
	xe_vm_assert_held(vops->vm);

	for (i = 0; i < pt_update_ops->current_op; ++i) {
		struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[i];

		xe_pt_free_bind(pt_op->entries, pt_op->num_entries);
	}
	xe_bo_put_commit(&vops->pt_update_ops[tile->id].deferred);
}

/**
 * xe_pt_update_ops_abort() - Abort PT update operations
 * @tile: Tile of PT update operations
 * @vops: VMA operationa
 *
 *  Abort PT update operations by unwinding internal PT state
 */
void xe_pt_update_ops_abort(struct xe_tile *tile, struct xe_vma_ops *vops)
{
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&vops->pt_update_ops[tile->id];
	int i;

	lockdep_assert_held(&vops->vm->lock);
	xe_vm_assert_held(vops->vm);

	for (i = pt_update_ops->num_ops - 1; i >= 0; --i) {
		struct xe_vm_pgtable_update_op *pt_op =
			&pt_update_ops->ops[i];

		if (!pt_op->vma || i >= pt_update_ops->current_op)
			continue;

		if (pt_op->bind)
			xe_pt_abort_bind(pt_op->vma, pt_op->entries,
					 pt_op->num_entries,
					 pt_op->rebind);
		else
			xe_pt_abort_unbind(pt_op->vma, pt_op->entries,
					   pt_op->num_entries);
	}

	xe_pt_update_ops_fini(tile, vops);
}
