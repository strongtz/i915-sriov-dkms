/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_BO_TYPES_H_
#define _XE_BO_TYPES_H_

#include <linux/iosys-map.h>

#include <drm/drm_gpusvm.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>

#include "xe_device_types.h"
#include "xe_ggtt_types.h"

struct xe_device;
struct xe_vm;

#define XE_BO_MAX_PLACEMENTS	3

/* TODO: To be selected with VM_MADVISE */
#define	XE_BO_PRIORITY_NORMAL	1

/** @xe_bo: XE buffer object */
struct xe_bo {
	/** @ttm: TTM base buffer object */
	struct ttm_buffer_object ttm;
	/** @backup_obj: The backup object when pinned and suspended (vram only) */
	struct xe_bo *backup_obj;
	/** @parent_obj: Ref to parent bo if this a backup_obj */
	struct xe_bo *parent_obj;
	/** @flags: flags for this buffer object */
	u32 flags;
	/** @vm: VM this BO is attached to, for extobj this will be NULL */
	struct xe_vm *vm;
	/** @tile: Tile this BO is attached to (kernel BO only) */
	struct xe_tile *tile;
	/** @placements: valid placements for this BO */
	struct ttm_place placements[XE_BO_MAX_PLACEMENTS];
	/** @placement: current placement for this BO */
	struct ttm_placement placement;
	/** @ggtt_node: Array of GGTT nodes if this BO is mapped in the GGTTs */
	struct xe_ggtt_node *ggtt_node[XE_MAX_TILES_PER_DEVICE];
	/** @vmap: iosys map of this buffer */
	struct iosys_map vmap;
	/** @ttm_kmap: TTM bo kmap object for internal use only. Keep off. */
	struct ttm_bo_kmap_obj kmap;
	/** @pinned_link: link to present / evicted list of pinned BO */
	struct list_head pinned_link;
#ifdef CONFIG_PROC_FS
	/**
	 * @client: @xe_drm_client which created the bo
	 */
	struct xe_drm_client *client;
	/**
	 * @client_link: Link into @xe_drm_client.objects_list
	 */
	struct list_head client_link;
#endif
	/**
	 * @pxp_key_instance: PXP key instance this BO was created against. A
	 * 0 in this variable indicates that the BO does not use PXP encryption.
	 */
	u32 pxp_key_instance;

	/** @freed: List node for delayed put. */
	struct llist_node freed;
	/** @update_index: Update index if PT BO */
	int update_index;
	/** @created: Whether the bo has passed initial creation */
	bool created;

	/** @ccs_cleared */
	bool ccs_cleared;

	/**
	 * @cpu_caching: CPU caching mode. Currently only used for userspace
	 * objects. Exceptions are system memory on DGFX, which is always
	 * WB.
	 */
	u16 cpu_caching;

	/** @devmem_allocation: SVM device memory allocation */
	struct drm_pagemap_devmem devmem_allocation;

	/** @vram_userfault_link: Link into @mem_access.vram_userfault.list */
		struct list_head vram_userfault_link;

	/** @min_align: minimum alignment needed for this BO if different
	 * from default
	 */
	u64 min_align;
};

#endif
