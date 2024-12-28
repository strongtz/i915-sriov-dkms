// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "abi/iov_actions_mmio_abi.h"
#include "intel_iov_ggtt.h"
#include "intel_iov_types.h"
#include "intel_iov_query.h"
#include "intel_iov_utils.h"

static gen8_pte_t prepare_pattern_pte(gen8_pte_t source_pte, u16 vfid)
{
	return (source_pte & MTL_GGTT_PTE_PAT_MASK) | i915_ggtt_prepare_vf_pte(vfid);
}

static struct scatterlist *
sg_add_addr(struct sg_table *st, struct scatterlist *sg, dma_addr_t addr)
{
	if (!sg)
		sg = st->sgl;

	st->nents++;
	sg_set_page(sg, NULL, I915_GTT_PAGE_SIZE, 0);
	sg_dma_address(sg) = addr;
	sg_dma_len(sg) = I915_GTT_PAGE_SIZE;
	return sg_next(sg);
}

static struct scatterlist *
sg_add_ptes(struct sg_table *st, struct scatterlist *sg, gen8_pte_t source_pte, u16 count,
	    bool duplicated)
{
	dma_addr_t pfn = FIELD_GET(GEN12_GGTT_PTE_ADDR_MASK, source_pte);

	while (count--)
		if (duplicated)
			sg = sg_add_addr(st, sg, pfn << PAGE_SHIFT);
		else
			sg = sg_add_addr(st, sg, pfn++ << PAGE_SHIFT);

	return sg;
}

static struct scatterlist *
sg_add_pte(struct sg_table *st, struct scatterlist *sg, gen8_pte_t source_pte)
{
	return sg_add_ptes(st, sg, source_pte, 1, false);
}

int intel_iov_ggtt_pf_update_vf_ptes(struct intel_iov *iov, u32 vfid, u32 pte_offset, u8 mode,
				     u16 num_copies, gen8_pte_t *ptes, u16 count)
{
	struct drm_mm_node *node = &iov->pf.provisioning.configs[vfid].ggtt_region;
	u64 ggtt_addr = node->start + pte_offset * I915_GTT_PAGE_SIZE_4K;
	u64 ggtt_addr_end = ggtt_addr + count * I915_GTT_PAGE_SIZE_4K - 1;
	u64 vf_ggtt_end = node->start + node->size - 1;
	gen8_pte_t pte_pattern = prepare_pattern_pte(*(ptes), vfid);
	struct sg_table *st;
	struct scatterlist *sg;
	bool is_duplicated;
	u16 n_ptes;
	int err;
	int i;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	/* XXX: All PTEs must have the same flags */
	for (i = 0; i < count; i++)
		GEM_BUG_ON(prepare_pattern_pte(ptes[i], vfid) != pte_pattern);

	if (!count)
		return -EINVAL;

	if (ggtt_addr_end > vf_ggtt_end)
		return -ERANGE;

	n_ptes = num_copies ? num_copies + count : count;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	if (sg_alloc_table(st, n_ptes, GFP_KERNEL)) {
		kfree(st);
		return -ENOMEM;
	}

	sg = st->sgl;
	st->nents = 0;

	/*
	 * To simplify the code, always let at least one PTE be updated by
	 * a function to duplicate or replicate.
	 */
	num_copies++;
	count--;

	is_duplicated = mode == MMIO_UPDATE_GGTT_MODE_DUPLICATE ||
			mode == MMIO_UPDATE_GGTT_MODE_DUPLICATE_LAST;

	switch (mode) {
	case MMIO_UPDATE_GGTT_MODE_DUPLICATE:
	case MMIO_UPDATE_GGTT_MODE_REPLICATE:
		sg = sg_add_ptes(st, sg, *(ptes++), num_copies, is_duplicated);

		while (count--)
			sg = sg_add_pte(st, sg, *(ptes++));
		break;
	case MMIO_UPDATE_GGTT_MODE_DUPLICATE_LAST:
	case MMIO_UPDATE_GGTT_MODE_REPLICATE_LAST:
		while (count--)
			sg = sg_add_pte(st, sg, *(ptes++));

		sg = sg_add_ptes(st, sg, *(ptes++), num_copies, is_duplicated);
		break;
	default:
		err = -EINVAL;
		goto cleanup;
	}

	err = i915_ggtt_sgtable_update_ptes(iov_to_gt(iov)->ggtt, vfid, ggtt_addr, st, n_ptes,
					    pte_pattern);
cleanup:
	sg_free_table(st);
	kfree(st);
	if (err < 0)
		return err;

	IOV_DEBUG(iov, "PF updated GGTT for %d PTE(s) from VF%u\n", n_ptes, vfid);
	return n_ptes;
}

void intel_iov_ggtt_vf_init_early(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	mutex_init(&iov->vf.ptes_buffer.lock);
}

void intel_iov_ggtt_vf_release(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	mutex_destroy(&iov->vf.ptes_buffer.lock);
}

static bool is_next_ggtt_offset(struct intel_iov *iov, u32 offset)
{
	struct intel_iov_vf_ggtt_ptes *buffer = &iov->vf.ptes_buffer;

	return (offset - (buffer->num_copies + buffer->count) == buffer->offset);
}

static bool is_pte_duplicatable(struct intel_iov *iov, gen8_pte_t pte)
{
	struct intel_iov_vf_ggtt_ptes *buffer = &iov->vf.ptes_buffer;

	return (buffer->ptes[buffer->count - 1] == pte);
}

static bool is_pte_replicable(struct intel_iov *iov, gen8_pte_t pte)
{
	struct intel_iov_vf_ggtt_ptes *buffer = &iov->vf.ptes_buffer;

	u64 new_gfn = FIELD_GET(GEN12_GGTT_PTE_ADDR_MASK, pte);
	u64 buffer_gfn = FIELD_GET(GEN12_GGTT_PTE_ADDR_MASK, buffer->ptes[buffer->count - 1]);
	u64 new_flags = FIELD_GET(MTL_GGTT_PTE_PAT_MASK, pte);
	u64 buffer_flags = FIELD_GET(MTL_GGTT_PTE_PAT_MASK, buffer->ptes[buffer->count - 1]);

	return (new_flags == buffer_flags && new_gfn - (buffer->num_copies + 1) == buffer_gfn);
}

void intel_iov_ggtt_vf_update_pte(struct intel_iov *iov, u32 offset, gen8_pte_t pte)
{
	struct intel_iov_vf_ggtt_ptes *buffer = &iov->vf.ptes_buffer;
	u16 max_copies = FIELD_MAX(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES);
	u16 max_ptes = MMIO_UPDATE_GGTT_MAX_PTES;
	u32 pte_offset = (offset >> PAGE_SHIFT) - (iov->vf.config.ggtt_base >> PAGE_SHIFT);

	BUILD_BUG_ON(MMIO_UPDATE_GGTT_MODE_DUPLICATE != VF2PF_UPDATE_GGTT32_MODE_DUPLICATE);
	BUILD_BUG_ON(MMIO_UPDATE_GGTT_MODE_REPLICATE != VF2PF_UPDATE_GGTT32_MODE_REPLICATE);
	BUILD_BUG_ON(MMIO_UPDATE_GGTT_MODE_DUPLICATE_LAST !=
		     VF2PF_UPDATE_GGTT32_MODE_DUPLICATE_LAST);
	BUILD_BUG_ON(MMIO_UPDATE_GGTT_MODE_REPLICATE_LAST !=
		     VF2PF_UPDATE_GGTT32_MODE_REPLICATE_LAST);

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	if (intel_guc_ct_enabled(&iov_to_guc(iov)->ct))
		max_ptes = VF2PF_UPDATE_GGTT_MAX_PTES;

	if (!buffer->count) {
		buffer->offset = pte_offset;
		buffer->ptes[0] = pte;
		buffer->count = 1;
		buffer->num_copies = 0;
		/**
		 * If num_copies is equal to 0, then the value
		 * of the MODE field is no matter.
		 * Let's set MODE as invalid so that we can check later
		 * if this field is set as expected.
		 */
		buffer->mode = VF_RELAY_UPDATE_GGTT_MODE_INVALID;
	} else if (!is_next_ggtt_offset(iov, pte_offset) || buffer->num_copies == max_copies) {
		goto flush;
	} else if (!buffer->num_copies) {
		if (is_pte_duplicatable(iov, pte)) {
			buffer->mode = (buffer->count == 1) ? MMIO_UPDATE_GGTT_MODE_DUPLICATE :
							      MMIO_UPDATE_GGTT_MODE_DUPLICATE_LAST;
			buffer->num_copies++;
		} else if (is_pte_replicable(iov, pte)) {
			buffer->mode = (buffer->count == 1) ? MMIO_UPDATE_GGTT_MODE_REPLICATE :
							      MMIO_UPDATE_GGTT_MODE_REPLICATE_LAST;
			buffer->num_copies++;
		} else {
			if (buffer->count == max_ptes)
				goto flush;

			buffer->ptes[buffer->count++] = pte;
		}
	} else if (buffer->count == 1 &&
		   buffer->mode == MMIO_UPDATE_GGTT_MODE_DUPLICATE &&
		   is_pte_duplicatable(iov, pte)) {
		buffer->num_copies++;
	} else if (buffer->count == 1 &&
		   buffer->mode == MMIO_UPDATE_GGTT_MODE_REPLICATE &&
		   is_pte_replicable(iov, pte)) {
		buffer->num_copies++;
	} else if (buffer->mode == MMIO_UPDATE_GGTT_MODE_DUPLICATE_LAST &&
		   is_pte_duplicatable(iov, pte)) {
		buffer->num_copies++;
	} else if (buffer->mode == MMIO_UPDATE_GGTT_MODE_REPLICATE_LAST &&
		 is_pte_replicable(iov, pte)) {
		buffer->num_copies++;
	/*
	 * If we operate in a mode that is not *_LAST
	 * (according to the ABI below the value of 2), then we
	 * have a chance to add some more PTEs to our request before
	 * send.
	 */
	} else if (buffer->mode < 2 && buffer->count != max_ptes) {
		buffer->ptes[buffer->count++] = pte;
	} else {
		goto flush;
	}

	return;
flush:
	intel_iov_ggtt_vf_flush_ptes(iov);
	intel_iov_ggtt_vf_update_pte(iov, offset, pte);
}

void intel_iov_ggtt_vf_flush_ptes(struct intel_iov *iov)
{
	struct intel_iov_vf_ggtt_ptes *buffer = &iov->vf.ptes_buffer;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	if (!buffer->count)
		return;

	intel_iov_query_update_ggtt_ptes(iov);
	buffer->count = 0;
}

/**
 * intel_iov_ggtt_shadow_init - allocate general shadow GGTT resources
 * @iov: the &struct intel_iov
 *
 * Return: 0 if success. Negative error code on error.
 */
int intel_iov_ggtt_shadow_init(struct intel_iov *iov)
{
	struct intel_iov_ggtt_shadow *vfs_shadows_ggtt;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(iov->pf.ggtt.shadows_ggtt);

	vfs_shadows_ggtt = kcalloc(1 + pf_get_totalvfs(iov), sizeof(*vfs_shadows_ggtt), GFP_KERNEL);
	if (unlikely(!vfs_shadows_ggtt))
		return -ENOMEM;

	iov->pf.ggtt.shadows_ggtt = vfs_shadows_ggtt;

	return 0;
}

/**
 * intel_iov_ggtt_shadow_fini - free general shadow  resources
 * @iov: the &struct intel_iov
 */
void intel_iov_ggtt_shadow_fini(struct intel_iov *iov)
{
	kfree(iov->pf.ggtt.shadows_ggtt);
	iov->pf.ggtt.shadows_ggtt = NULL;
}

/**
 * intel_iov_ggtt_shadow_vf_alloc - allocate VF shadow GGTT resources
 * @iov: the &struct intel_iov
 * @vfid: VF ID
 * @ggtt_region: pointer to VF GGTT region
 *
 * Return: 0 if success or shadow GGTT not initialized. Negative error code on error.
 */
int intel_iov_ggtt_shadow_vf_alloc(struct intel_iov *iov, unsigned int vfid,
				   struct drm_mm_node *ggtt_region)
{
	gen8_pte_t *ptes;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.ggtt.shadows_ggtt)
		return 0;

	GEM_BUG_ON(!drm_mm_node_allocated(ggtt_region));
	GEM_BUG_ON(iov->pf.ggtt.shadows_ggtt[vfid].ptes);

	ptes = kvzalloc(ggtt_size_to_ptes_size(ggtt_region->size), GFP_KERNEL);
	if (unlikely(!ptes))
		return -ENOMEM;

	iov->pf.ggtt.shadows_ggtt[vfid].ptes = ptes;
	iov->pf.ggtt.shadows_ggtt[vfid].ggtt_region = ggtt_region;
	iov->pf.ggtt.shadows_ggtt[vfid].vfid = vfid;

	return 0;
}

/**
 * intel_iov_ggtt_shadow_vf_free - free shadow GGTT resources allocated for VF
 * @iov: the &struct intel_iov
 * @vfid: VF ID
 *
 * Skip if shadow GGTT not initialized.
 */
void intel_iov_ggtt_shadow_vf_free(struct intel_iov *iov, unsigned int vfid)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.ggtt.shadows_ggtt)
		return;

	kvfree(iov->pf.ggtt.shadows_ggtt[vfid].ptes);
	iov->pf.ggtt.shadows_ggtt[vfid].ptes = NULL;
}

static u64 ggtt_addr_to_pte_offset(u64 ggtt_addr)
{
	GEM_BUG_ON(!IS_ALIGNED(ggtt_addr, I915_GTT_PAGE_SIZE_4K));

	return (ggtt_addr / I915_GTT_PAGE_SIZE_4K) * sizeof(gen8_pte_t);
}

static u64 pf_ggtt_addr_to_vf_pte_offset(struct intel_iov *iov, unsigned int vfid, u64 ggtt_addr)
{
	struct drm_mm_node *ggtt_region;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	ggtt_region = iov->pf.ggtt.shadows_ggtt[vfid].ggtt_region;

	GEM_BUG_ON(ggtt_region->start > ggtt_addr ||
		   ggtt_region->start + ggtt_region->size <= ggtt_addr);

	return ggtt_addr_to_pte_offset(ggtt_addr - ggtt_region->start);
}

static gen8_pte_t *ggtt_shadow_get_pte_ptr(struct intel_iov *iov, unsigned int vfid, u64 ggtt_addr)
{
	u32 pte_idx;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(!IS_ALIGNED(ggtt_addr, sizeof(gen8_pte_t)));

	pte_idx = pf_ggtt_addr_to_vf_pte_offset(iov, vfid, ggtt_addr) / sizeof(gen8_pte_t);
	return &iov->pf.ggtt.shadows_ggtt[vfid].ptes[pte_idx];
}

/**
 * intel_iov_ggtt_shadow_set_pte - set VF GGTT PTE in shadow GGTT
 * @iov: the &struct intel_iov
 * @vfid: VF id
 * @ggtt_addr: GGTT address
 * @pte: PTE value to save
 *
 */
void intel_iov_ggtt_shadow_set_pte(struct intel_iov *iov, unsigned int vfid, u64 ggtt_addr,
					gen8_pte_t pte)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.ggtt.shadows_ggtt)
		return;

	memset64(ggtt_shadow_get_pte_ptr(iov, vfid, ggtt_addr), pte, 1);
}

/**
 * intel_iov_ggtt_shadow_get_pte - get VF GGTT PTE from shadow GGTT
 * @iov: the &struct intel_iov
 * @vfid: VF ID
 * @ggtt_addr: GGTT address
 *
 * Returns: PTE value for given @ggtt_addr, or 0 if shadow GGTT not initialized.
 */
gen8_pte_t intel_iov_ggtt_shadow_get_pte(struct intel_iov *iov, unsigned int vfid, u64 ggtt_addr)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.ggtt.shadows_ggtt)
		return 0;

	return *(ggtt_shadow_get_pte_ptr(iov, vfid, ggtt_addr));
}

/**
 * intel_iov_ggtt_shadow_save - copy VF GGTT PTEs to preallocated buffer
 * @iov: the &struct intel_iov
 * @vfid: VF id
 * @buf: preallocated buffer in which PTEs will be saved
 * @size: size of preallocated buffer (in bytes)
 *        - must be sizeof(gen8_pte_t) aligned
 * @flags: function flags:
 *         - #I915_GGTT_SAVE_PTES_NO_VFID BIT - save PTEs without VFID
 *
 * Returns: size of the buffer used (or needed if both @buf and @size are (0)) to store all PTEs
 *          for a given vfid, -EINVAL if one of @buf or @size is 0.
 */
int intel_iov_ggtt_shadow_save(struct intel_iov *iov, unsigned int vfid, void *buf, size_t size,
			       unsigned int flags)
{
	struct drm_mm_node *ggtt_region;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.ggtt.shadows_ggtt)
		return 0;

	ggtt_region = iov->pf.ggtt.shadows_ggtt[vfid].ggtt_region;

	if (!buf && !size)
		return ggtt_size_to_ptes_size(ggtt_region->size);

	if (!buf || !size)
		return -EINVAL;

	if (size > ggtt_size_to_ptes_size(ggtt_region->size))
		return -ENOSPC;

	GEM_BUG_ON(!IS_ALIGNED(size, sizeof(gen8_pte_t)));

	memcpy(buf, iov->pf.ggtt.shadows_ggtt[vfid].ptes, size);

	if (flags & I915_GGTT_SAVE_PTES_NO_VFID)
		ggtt_pte_clear_vfid(buf, size);

	return size;
}

static int pf_ggtt_shadow_restore_ggtt(struct intel_iov *iov, unsigned int vfid)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	u32 pte_count = 0;
	gen8_pte_t pte_flags = 0, new_pte_flags;
	struct drm_mm_node *ggtt_region;
	size_t size;
	u64 ggtt_addr;
	struct sg_table *st;
	struct scatterlist *sg;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.ggtt.shadows_ggtt)
		return 0;

	ggtt_region = iov->pf.ggtt.shadows_ggtt[vfid].ggtt_region;
	size = ggtt_size_to_ptes_size(ggtt_region->size);
	ggtt_addr = ggtt_region->start;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	if (sg_alloc_table(st, size / sizeof(gen8_pte_t), GFP_KERNEL)) {
		err = -ENOMEM;
		goto out_free_table;
	}

	sg = st->sgl;
	st->nents = 0;

	while (size) {
		gen8_pte_t pte = intel_iov_ggtt_shadow_get_pte(iov, vfid, ggtt_addr);

		new_pte_flags = pte & ~GEN12_GGTT_PTE_ADDR_MASK;
		if (pte_count && new_pte_flags != pte_flags) {
			err = i915_ggtt_sgtable_update_ptes(ggtt, vfid, ggtt_addr, st, pte_count,
							    pte_flags);
			if (err < 0)
				goto out_free_st;

			sg_free_table(st);
			if (sg_alloc_table(st, size / sizeof(gen8_pte_t), GFP_KERNEL)) {
				err = -ENOMEM;
				goto out_free_st;
			}

			pte_count = 0;
		}
		sg = sg_add_pte(st, sg, pte);

		pte_count++;
		pte_flags = new_pte_flags;
		ggtt_addr += I915_GTT_PAGE_SIZE_4K;
		size -= sizeof(gen8_pte_t);
	}

	err = i915_ggtt_sgtable_update_ptes(ggtt, vfid, ggtt_addr, st, pte_count, pte_flags);

out_free_table:
	sg_free_table(st);
out_free_st:
	kfree(st);
	return err;
}

/**
 * intel_iov_ggtt_shadow_restore() - restore GGTT PTEs from buffer
 * @iov: the &struct intel_iov
 * @vfid: VF id
 * @buf: buffer from which PTEs will be restored
 * @size: size of preallocated buffer (in bytes)
 *        - must be sizeof(gen8_pte_t) aligned
 * @flags: function flags:
 *         - #I915_GGTT_RESTORE_PTES_VFID_MASK - VFID for restored PTEs
 *         - #I915_GGTT_RESTORE_PTES_NEW_VFID - restore PTEs with new VFID
 *           (from #I915_GGTT_RESTORE_PTES_VFID_MASK)
 *
 * Returns: size of restored PTES on success, or negative error code.
 */
int intel_iov_ggtt_shadow_restore(struct intel_iov *iov, unsigned int vfid, const void *buf,
				       size_t size, unsigned int flags)
{
	struct drm_mm_node *ggtt_region;
	u64 ggtt_addr;
	size_t remain_size;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(flags & I915_GGTT_RESTORE_PTES_NEW_VFID &&
		   vfid != FIELD_GET(I915_GGTT_RESTORE_PTES_VFID_MASK, flags));
	GEM_BUG_ON(!IS_ALIGNED(size, sizeof(gen8_pte_t)));

	if (!iov->pf.ggtt.shadows_ggtt)
		return 0;

	ggtt_region = iov->pf.ggtt.shadows_ggtt[vfid].ggtt_region;

	if (size > ggtt_size_to_ptes_size(ggtt_region->size))
		return -ENOSPC;

	if (!buf || !size)
		return -EINVAL;

	ggtt_addr = ggtt_region->start;
	remain_size = size;

	while (remain_size) {
		gen8_pte_t pte = *(gen8_pte_t *)buf;

		if (flags & I915_GGTT_RESTORE_PTES_NEW_VFID)
			pte |= i915_ggtt_prepare_vf_pte(vfid);

		intel_iov_ggtt_shadow_set_pte(iov, vfid, ggtt_addr, pte);

		ggtt_addr += I915_GTT_PAGE_SIZE_4K;
		buf += sizeof(gen8_pte_t);
		remain_size -= sizeof(gen8_pte_t);
	}

	err = pf_ggtt_shadow_restore_ggtt(iov, vfid);

	return err ?: size;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/selftest_mock_iov_ggtt.c"
#endif
