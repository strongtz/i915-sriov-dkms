/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GGTT_TYPES_H_
#define _XE_GGTT_TYPES_H_

#include <linux/atomic.h>
#include <linux/workqueue.h>

#include <drm/drm_mm.h>

#include "abi/guc_relay_actions_abi.h"
#include "xe_pt_types.h"

struct xe_bo;
struct xe_gt;

/**
 * struct xe_ggtt - Main GGTT struct
 *
 * In general, each tile can contains its own Global Graphics Translation Table
 * (GGTT) instance.
 */
struct xe_ggtt {
	/** @tile: Back pointer to tile where this GGTT belongs */
	struct xe_tile *tile;
	/** @size: Total size of this GGTT */
	u64 size;

#define XE_GGTT_FLAGS_64K BIT(0)
	/**
	 * @flags: Flags for this GGTT
	 * Acceptable flags:
	 * - %XE_GGTT_FLAGS_64K - if PTE size is 64K. Otherwise, regular is 4K.
	 */
	unsigned int flags;
	/** @scratch: Internal object allocation used as a scratch page */
	struct xe_bo *scratch;
	/** @lock: Mutex lock to protect GGTT data */
	struct mutex lock;
	/**
	 *  @gsm: The iomem pointer to the actual location of the translation
	 * table located in the GSM for easy PTE manipulation
	 */
	u64 __iomem *gsm;
	/** @pt_ops: Page Table operations per platform */
	const struct xe_ggtt_pt_ops *pt_ops;
	/** @mm: The memory manager used to manage individual GGTT allocations */
	struct drm_mm mm;
	/** @access_count: counts GGTT writes */
	unsigned int access_count;
	/** @wq: Dedicated unordered work queue to process node removals */
	struct workqueue_struct *wq;
	/** @invalidate_work: Deferred GGTT invalidate work */
	struct work_struct invalidate_work;
	/** @invalidate_pending: Pending invalidate request */
	atomic_t invalidate_pending;
#ifdef CONFIG_PCI_IOV
	/** @vf_relay_ready: use explicit VF->PF relay for steady-state GGTT updates */
	bool vf_relay_ready;
	/**
	 * @vf_ptes: VF-side buffered GGTT update state.
	 *
	 * Matches the i915 VF sender model: accumulate sequential PTE updates
	 * and only flush a message when the run shape can no longer be extended.
	 */
	struct {
		/** @vf_ptes.lock: Protects buffered GGTT update state. */
		struct mutex lock;
		/** @vf_ptes.offset: Buffered GGTT PTE offset relative to VF base. */
		u32 offset;
		/** @vf_ptes.count: Number of literal PTEs currently buffered. */
		u16 count;
		/** @vf_ptes.num_copies: Number of duplicate/replicate copies. */
		u16 num_copies;
		/** @vf_ptes.mode: Encoded duplicate/replicate mode for the buffer. */
		u8 mode;
		/** @vf_ptes.ptes: Buffered literal PTE payload. */
		u64 ptes[VF2PF_UPDATE_GGTT_MAX_PTES];
	} vf_ptes;
#endif
};

/**
 * struct xe_ggtt_node - A node in GGTT.
 *
 * This struct needs to be initialized (only-once) with xe_ggtt_node_init() before any node
 * insertion, reservation, or 'ballooning'.
 * It will, then, be finalized by either xe_ggtt_node_remove() or xe_ggtt_node_deballoon().
 */
struct xe_ggtt_node {
	/** @ggtt: Back pointer to xe_ggtt where this region will be inserted at */
	struct xe_ggtt *ggtt;
	/** @base: A drm_mm_node */
	struct drm_mm_node base;
	/** @delayed_removal_work: The work struct for the delayed removal */
	struct work_struct delayed_removal_work;
	/** @invalidate_on_remove: If it needs invalidation upon removal */
	bool invalidate_on_remove;
#ifdef CONFIG_PCI_IOV
	/** @vf_shadow_ptes: MTL-only PF shadow of VF GGTT contents, without VFID bits */
	u64 *vf_shadow_ptes;
	/** @vf_shadow_len: Number of PTEs tracked in @vf_shadow_ptes */
	u32 vf_shadow_len;
	/** @vfid: VF identifier assigned to this GGTT region */
	u16 vfid;
	/** @vf_apply_work: MTL-only staged PF GGTT apply worker */
	struct work_struct vf_apply_work;
	/** @vf_apply_dirty: Whether staged PF GGTT updates are pending */
	bool vf_apply_dirty;
	/** @vf_apply_queued: Whether @vf_apply_work is queued or running */
	bool vf_apply_queued;
	/** @vf_apply_start: Dirty PTE start offset, inclusive */
	u32 vf_apply_start;
	/** @vf_apply_end: Dirty PTE end offset, exclusive */
	u32 vf_apply_end;
#endif
};

/**
 * struct xe_ggtt_pt_ops - GGTT Page table operations
 * Which can vary from platform to platform.
 */
struct xe_ggtt_pt_ops {
	/** @pte_encode_flags: Encode PTE flags for a given BO */
	u64 (*pte_encode_flags)(struct xe_bo *bo, u16 pat_index);
	/** @ggtt_set_pte: Directly write into GGTT's PTE */
	void (*ggtt_set_pte)(struct xe_ggtt *ggtt, u64 addr, u64 pte);
	/** @ggtt_get_pte: Directly read from GGTT's PTE */
	u64 (*ggtt_get_pte)(struct xe_ggtt *ggtt, u64 addr);
};

#endif
