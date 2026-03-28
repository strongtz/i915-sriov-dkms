// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "abi/guc_relay_actions_abi.h"
#include "abi/iov_actions_mmio_abi.h"

#include "xe_ggtt.h"

#include <kunit/visibility.h>
#include <linux/fault-inject.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/ratelimit.h>
#include <linux/sizes.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/intel/i915_drm.h>
#include <generated/xe_wa_oob.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_guc_regs.h"
#include "regs/xe_gtt_defs.h"
#include "regs/xe_regs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_force_wake.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_res_cursor.h"
#include "xe_sriov.h"
#include "xe_guc_relay.h"
#include "xe_tile_printk.h"
#include "xe_tile_sriov_vf.h"
#include "xe_tlb_inval.h"
#include "xe_wa.h"
#include "xe_wopcm.h"

/**
 * DOC: Global Graphics Translation Table (GGTT)
 *
 * Xe GGTT implements the support for a Global Virtual Address space that is used
 * for resources that are accessible to privileged (i.e. kernel-mode) processes,
 * and not tied to a specific user-level process. For example, the Graphics
 * micro-Controller (GuC) and Display Engine (if present) utilize this Global
 * address space.
 *
 * The Global GTT (GGTT) translates from the Global virtual address to a physical
 * address that can be accessed by HW. The GGTT is a flat, single-level table.
 *
 * Xe implements a simplified version of the GGTT specifically managing only a
 * certain range of it that goes from the Write Once Protected Content Memory (WOPCM)
 * Layout to a predefined GUC_GGTT_TOP. This approach avoids complications related to
 * the GuC (Graphics Microcontroller) hardware limitations. The GuC address space
 * is limited on both ends of the GGTT, because the GuC shim HW redirects
 * accesses to those addresses to other HW areas instead of going through the
 * GGTT. On the bottom end, the GuC can't access offsets below the WOPCM size,
 * while on the top side the limit is fixed at GUC_GGTT_TOP. To keep things
 * simple, instead of checking each object to see if they are accessed by GuC or
 * not, we just exclude those areas from the allocator. Additionally, to simplify
 * the driver load, we use the maximum WOPCM size in this logic instead of the
 * programmed one, so we don't need to wait until the actual size to be
 * programmed is determined (which requires FW fetch) before initializing the
 * GGTT. These simplifications might waste space in the GGTT (about 20-25 MBs
 * depending on the platform) but we can live with this. Another benefit of this
 * is the GuC bootrom can't access anything below the WOPCM max size so anything
 * the bootrom needs to access (e.g. a RSA key) needs to be placed in the GGTT
 * above the WOPCM max size. Starting the GGTT allocations above the WOPCM max
 * give us the correct placement for free.
 */

static u64 xelp_ggtt_pte_flags(struct xe_bo *bo, u16 pat_index)
{
	u64 pte = XE_PAGE_PRESENT;

	if (xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo))
		pte |= XE_GGTT_PTE_DM;

	return pte;
}

static u64 xelpg_ggtt_pte_flags(struct xe_bo *bo, u16 pat_index)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pte;

	pte = xelp_ggtt_pte_flags(bo, pat_index);

	xe_assert(xe, pat_index <= 3);

	if (pat_index & BIT(0))
		pte |= XELPG_GGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XELPG_GGTT_PTE_PAT1;

	return pte;
}

static unsigned int probe_gsm_size(struct pci_dev *pdev)
{
	u16 gmch_ctl, ggms;

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &gmch_ctl);
	ggms = (gmch_ctl >> BDW_GMCH_GGMS_SHIFT) & BDW_GMCH_GGMS_MASK;
	return ggms ? SZ_1M << ggms : 0;
}

static bool xe_ggtt_allow_direct_gsm_access(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	/*
	 * Mirror i915's MTL PF preference as closely as possible: if firmware
	 * has relaxed stolen/GSM access permissions, bypass the BAR-backed
	 * GGTT window and access GSM directly.
	 */
	if (MEDIA_VERx100(xe) != 1300 || IS_DGFX(xe) || IS_SRIOV_VF(xe))
		return false;

	if (ggtt->tile != xe_device_get_root_tile(xe))
		return false;

	return xe_mmio_read32(xe_root_tile_mmio(xe), MTL_PCODE_STOLEN_ACCESS) ==
	       STOLEN_ACCESS_ALLOWED;
}

static u64 __iomem *xe_ggtt_map_gsm(struct xe_ggtt *ggtt, u64 gsm_size)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	phys_addr_t phys_addr;

	if (!xe_ggtt_allow_direct_gsm_access(ggtt))
		return ggtt->tile->mmio.regs + SZ_8M;

	drm_info_once(&xe->drm,
		      "xe: MTL GGTT path: PF uses i915-like direct GSM mapping for GGTT PTE access\n");

	phys_addr = xe_mmio_read64_2x32(xe_root_tile_mmio(xe), GSMBASE) & BDSM_MASK;

	return devm_ioremap(xe->drm.dev, phys_addr, gsm_size);
}

static void ggtt_update_access_counter(struct xe_ggtt *ggtt)
{
	struct xe_tile *tile = ggtt->tile;
	struct xe_gt *affected_gt;
	u32 max_gtt_writes;

	if (tile->primary_gt && XE_GT_WA(tile->primary_gt, 22019338487)) {
		affected_gt = tile->primary_gt;
		max_gtt_writes = 1100;

		/* Only expected to apply to primary GT on dgpu platforms */
		xe_tile_assert(tile, IS_DGFX(tile_to_xe(tile)));
	} else {
		affected_gt = tile->media_gt;
		max_gtt_writes = 63;

		/* Only expected to apply to media GT on igpu platforms */
		xe_tile_assert(tile, !IS_DGFX(tile_to_xe(tile)));
	}

	/*
	 * Wa_22019338487: GMD_ID is a RO register, a dummy write forces gunit
	 * to wait for completion of prior GTT writes before letting this through.
	 * This needs to be done for all GGTT writes originating from the CPU.
	 */
	lockdep_assert_held(&ggtt->lock);

	if ((++ggtt->access_count % max_gtt_writes) == 0) {
		xe_mmio_write32(&affected_gt->mmio, GMD_ID, 0x0);
		ggtt->access_count = 0;
	}
}

static void xe_ggtt_set_pte(struct xe_ggtt *ggtt, u64 addr, u64 pte)
{
	xe_tile_assert(ggtt->tile, !(addr & XE_PTE_MASK));
	xe_tile_assert(ggtt->tile, addr < ggtt->size);

	writeq(pte, &ggtt->gsm[addr >> XE_PTE_SHIFT]);
}

static void xe_ggtt_set_pte_and_flush(struct xe_ggtt *ggtt, u64 addr, u64 pte)
{
	xe_ggtt_set_pte(ggtt, addr, pte);
	ggtt_update_access_counter(ggtt);
}

static u64 xe_ggtt_get_pte(struct xe_ggtt *ggtt, u64 addr)
{
	xe_tile_assert(ggtt->tile, !(addr & XE_PTE_MASK));
	xe_tile_assert(ggtt->tile, addr < ggtt->size);

	return readq(&ggtt->gsm[addr >> XE_PTE_SHIFT]);
}

static void ggtt_invalidate_work_func(struct work_struct *work);
static void ggtt_vf_apply_work_func(struct work_struct *work);

#ifdef CONFIG_PCI_IOV
#define GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE			0x5005u
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MIN_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MAX_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 3u)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC		(0xfu << 24)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_OPCODE		(0xffu << 16)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MIN_LEN		(GUC_HXG_RESPONSE_MSG_MIN_LEN)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MAX_LEN		(GUC_HXG_RESPONSE_MSG_MIN_LEN + 3u)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_0_MAGIC		(0xfu << 24)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_0_DATA0		(0xffffffu << 0)

#define XE_GGTT_PTE_ADDR_MASK					GENMASK_ULL(51, 12)

static bool xe_ggtt_use_mtl_pf_mmio_invalidate(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	return xe_device_needs_mtl_ggtt_binder(xe) && IS_SRIOV_PF(xe);
}

static struct xe_gt *xe_ggtt_vf_relay_gt(struct xe_ggtt *ggtt)
{
	return ggtt->tile->primary_gt ?: ggtt->tile->media_gt;
}

static bool xe_ggtt_vf_explicit_enabled(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	if (!xe_device_needs_mtl_ggtt_binder(xe) || !IS_SRIOV_VF(xe) ||
	    !ggtt->vf_relay_ready || !xe->sriov.vf.pf_version.major)
		return false;

	return xe_ggtt_vf_relay_gt(ggtt) != NULL;
}

static bool xe_ggtt_vf_relay_enabled(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	struct xe_gt *gt;

	if (!xe_ggtt_vf_explicit_enabled(ggtt))
		return false;

	gt = xe_ggtt_vf_relay_gt(ggtt);

	if (!xe_guc_ct_enabled(&gt->uc.guc.ct) || !gt->uc.guc.submission_state.enabled)
		return false;

	drm_info_once(&xe->drm,
		      "xe: MTL SR-IOV GGTT path: VF steady-state relay enabled after GuC CT submission\n");

	return true;
}

static bool xe_ggtt_vf_mmio_enabled(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	if (!xe_ggtt_vf_explicit_enabled(ggtt) || xe_ggtt_vf_relay_enabled(ggtt))
		return false;

	drm_info_once(&xe->drm,
		      "xe: MTL SR-IOV GGTT path: VF MMIO bootstrap GGTT sender active before GuC CT submission\n");

	return true;
}

static int xe_ggtt_vf_mmio_send_pte(struct xe_ggtt *ggtt, u64 ggtt_addr,
				    u8 mode, u16 num_copies, u64 pte)
{
	struct xe_gt *gt = xe_ggtt_vf_relay_gt(ggtt);
	u64 vf_base = xe_tile_sriov_vf_ggtt_base(ggtt->tile);
	u32 response[VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MAX_LEN];
	u32 request[VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MAX_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE) |
		FIELD_PREP(VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC, 0xfu) |
		FIELD_PREP(VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_OPCODE,
			   IOV_OPCODE_VF2PF_MMIO_UPDATE_GGTT),
		FIELD_PREP(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_MODE, mode) |
		FIELD_PREP(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES, num_copies) |
		FIELD_PREP(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_OFFSET,
			   (ggtt_addr - vf_base) >> XE_PTE_SHIFT),
		FIELD_PREP(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_2_PTE_LO, lower_32_bits(pte)),
		FIELD_PREP(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_3_PTE_HI, upper_32_bits(pte)),
	};
	u32 magic1, magic2;
	u16 expected = num_copies + 1;
	u16 updated;
	int ret;

	if (XE_WARN_ON(!gt))
		return -ENODEV;
	if (XE_WARN_ON(ggtt_addr < vf_base))
		return -ERANGE;
	if (XE_WARN_ON(FIELD_MAX(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_MODE) < mode))
		return -EINVAL;
	if (XE_WARN_ON(FIELD_MAX(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES) < num_copies))
		return -EINVAL;

	magic1 = FIELD_GET(VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC, request[0]);

	ret = xe_guc_mmio_send_recv(&gt->uc.guc, request, ARRAY_SIZE(request), response);
	if (ret < 0)
		return ret;

	magic2 = FIELD_GET(VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_0_MAGIC, response[0]);
	if (magic1 != magic2)
		return -EPROTO;

	updated = FIELD_GET(VF2PF_MMIO_UPDATE_GGTT_RESPONSE_MSG_1_NUM_PTES, response[0]);

	return updated == expected ? 0 : -EPROTO;
}

static bool xe_ggtt_pte_duplicatable(u64 prev_pte, u64 pte)
{
	return prev_pte == pte;
}

static bool xe_ggtt_pte_replicable(u64 prev_pte, u64 pte)
{
	u64 prev_addr = prev_pte & XE_GGTT_PTE_ADDR_MASK;
	u64 addr = pte & XE_GGTT_PTE_ADDR_MASK;
	u64 prev_flags = prev_pte & ~XE_GGTT_PTE_ADDR_MASK;
	u64 flags = pte & ~XE_GGTT_PTE_ADDR_MASK;

	return flags == prev_flags && addr == prev_addr + XE_PAGE_SIZE;
}

static int xe_ggtt_vf_mmio_clear(struct xe_ggtt *ggtt, u64 start, u64 size, u64 scratch_pte)
{
	u64 addr = start;
	u64 remaining = size / XE_PAGE_SIZE;
	u64 max_entries = FIELD_MAX(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES) + 1;
	int ret;

	while (remaining) {
		u64 entries = min(remaining, max_entries);

		ret = xe_ggtt_vf_mmio_send_pte(ggtt, addr, MMIO_UPDATE_GGTT_MODE_DUPLICATE,
					       entries - 1, scratch_pte);
		if (ret)
			return ret;

		addr += entries * XE_PAGE_SIZE;
		remaining -= entries;
	}

	return 0;
}

static int xe_ggtt_vf_mmio_send_ptes(struct xe_ggtt *ggtt, u64 start,
				     const u64 *ptes, u16 count)
{
	u64 run_addr = start;
	u64 run_pte;
	u16 run_len = 1;
	u16 max_entries = FIELD_MAX(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES) + 1;
	u8 run_mode = MMIO_UPDATE_GGTT_MODE_DUPLICATE;
	int ret;
	u16 i;

	if (XE_WARN_ON(!count))
		return -EINVAL;

	run_pte = ptes[0];

	for (i = 1; i < count; i++) {
		bool duplicatable = xe_ggtt_pte_duplicatable(run_pte + (run_mode == MMIO_UPDATE_GGTT_MODE_REPLICATE ?
								       (u64)(run_len - 1) * XE_PAGE_SIZE : 0),
							       ptes[i]);
		bool replicable = xe_ggtt_pte_replicable(run_pte + (run_mode == MMIO_UPDATE_GGTT_MODE_REPLICATE ?
								      (u64)(run_len - 1) * XE_PAGE_SIZE : 0),
							      ptes[i]);

		if (run_len < max_entries) {
			if (run_len == 1 && duplicatable) {
				run_mode = MMIO_UPDATE_GGTT_MODE_DUPLICATE;
				run_len++;
				continue;
			}

			if (run_len == 1 && replicable) {
				run_mode = MMIO_UPDATE_GGTT_MODE_REPLICATE;
				run_len++;
				continue;
			}

			if (run_mode == MMIO_UPDATE_GGTT_MODE_DUPLICATE && duplicatable) {
				run_len++;
				continue;
			}

			if (run_mode == MMIO_UPDATE_GGTT_MODE_REPLICATE && replicable) {
				run_len++;
				continue;
			}
		}

		ret = xe_ggtt_vf_mmio_send_pte(ggtt, run_addr, run_mode, run_len - 1, run_pte);
		if (ret)
			return ret;

		run_addr = start + (u64)i * XE_PAGE_SIZE;
		run_pte = ptes[i];
		run_len = 1;
		run_mode = MMIO_UPDATE_GGTT_MODE_DUPLICATE;
	}

	return xe_ggtt_vf_mmio_send_pte(ggtt, run_addr, run_mode, run_len - 1, run_pte);
}

static int xe_ggtt_vf_relay_send_ptes(struct xe_ggtt *ggtt, u64 ggtt_addr,
				      u8 mode, u16 num_copies,
				      const u64 *ptes, u16 count)
{
	struct xe_gt *gt = xe_ggtt_vf_relay_gt(ggtt);
	u64 vf_base = xe_tile_sriov_vf_ggtt_base(ggtt->tile);
	u32 response[VF2PF_UPDATE_GGTT32_RESPONSE_MSG_LEN];
	u32 msg[VF2PF_UPDATE_GGTT32_REQUEST_MSG_MIN_LEN + VF2PF_UPDATE_GGTT_MAX_PTES * 2];
	u16 expected = num_copies ? num_copies + count : count;
	u16 updated;
	int i;
	int ret;

	if (XE_WARN_ON(!gt))
		return -ENODEV;
	if (XE_WARN_ON(ggtt_addr < vf_base))
		return -ERANGE;
	if (XE_WARN_ON(!count) || XE_WARN_ON(count > VF2PF_UPDATE_GGTT_MAX_PTES))
		return -EINVAL;

	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		 FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VF2PF_UPDATE_GGTT32);
	msg[1] = FIELD_PREP(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_OFFSET,
			    (ggtt_addr - vf_base) >> XE_PTE_SHIFT) |
		 FIELD_PREP(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_MODE, mode) |
		 FIELD_PREP(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_NUM_COPIES, num_copies);

	for (i = 0; i < count; i++) {
		msg[2 + i * 2] = lower_32_bits(ptes[i]);
		msg[3 + i * 2] = upper_32_bits(ptes[i]);
	}

	ret = xe_guc_relay_send_to_pf(&gt->uc.guc.relay, msg, 2 + count * 2,
				      response, ARRAY_SIZE(response));
	if (ret != VF2PF_UPDATE_GGTT32_RESPONSE_MSG_LEN)
		return ret < 0 ? ret : -EPROTO;

	updated = FIELD_GET(VF2PF_UPDATE_GGTT32_RESPONSE_MSG_0_NUM_PTES, response[0]);
	return updated == expected ? 0 : -EPROTO;
}

static int xe_ggtt_vf_explicit_send_ptes(struct xe_ggtt *ggtt, u64 ggtt_addr,
					 const u64 *ptes, u16 count)
{
	if (xe_ggtt_vf_relay_enabled(ggtt))
		return xe_ggtt_vf_relay_send_ptes(ggtt, ggtt_addr,
						  VF2PF_UPDATE_GGTT32_MODE_DUPLICATE_LAST,
						  0, ptes, count);

	if (xe_ggtt_vf_mmio_enabled(ggtt))
		return xe_ggtt_vf_mmio_send_ptes(ggtt, ggtt_addr, ptes, count);

	return -EOPNOTSUPP;
}

static int xe_ggtt_vf_relay_clear(struct xe_ggtt *ggtt, u64 start, u64 size, u64 scratch_pte)
{
	u64 addr = start;
	u64 remaining = size / XE_PAGE_SIZE;
	u64 max_entries = FIELD_MAX(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_NUM_COPIES) + 1;
	int ret;

	while (remaining) {
		u64 entries = min(remaining, max_entries);

		ret = xe_ggtt_vf_relay_send_ptes(ggtt, addr,
						 VF2PF_UPDATE_GGTT32_MODE_DUPLICATE,
						 entries - 1, &scratch_pte, 1);
		if (ret)
			return ret;

		addr += entries * XE_PAGE_SIZE;
		remaining -= entries;
	}

	return 0;
}

static int xe_ggtt_vf_explicit_clear(struct xe_ggtt *ggtt, u64 start, u64 size, u64 scratch_pte)
{
	if (xe_ggtt_vf_relay_enabled(ggtt))
		return xe_ggtt_vf_relay_clear(ggtt, start, size, scratch_pte);

	if (xe_ggtt_vf_mmio_enabled(ggtt))
		return xe_ggtt_vf_mmio_clear(ggtt, start, size, scratch_pte);

	return -EOPNOTSUPP;
}

static int xe_ggtt_vf_explicit_map_bo(struct xe_ggtt *ggtt, u64 start, struct xe_bo *bo, u64 pte)
{
	u64 ptes[VF2PF_UPDATE_GGTT_MAX_PTES];
	u64 addr = start;
	struct xe_res_cursor cur;
	u16 count = 0;
	int ret;

	if (!xe_bo_is_vram(bo) && !xe_bo_is_stolen(bo)) {
		xe_assert(xe_bo_device(bo), bo->ttm.ttm);

		for (xe_res_first_sg(xe_bo_sg(bo), 0, xe_bo_size(bo), &cur);
		     cur.remaining; xe_res_next(&cur, XE_PAGE_SIZE), addr += XE_PAGE_SIZE) {
			ptes[count++] = pte | xe_res_dma(&cur);
			if (count == VF2PF_UPDATE_GGTT_MAX_PTES) {
				u64 chunk_start = addr + XE_PAGE_SIZE - (u64)count * XE_PAGE_SIZE;

				ret = xe_ggtt_vf_explicit_send_ptes(ggtt, chunk_start, ptes, count);
				if (ret)
					return ret;
				count = 0;
			}
		}
	} else {
		pte |= vram_region_gpu_offset(bo->ttm.resource);

		for (xe_res_first(bo->ttm.resource, 0, xe_bo_size(bo), &cur);
		     cur.remaining; xe_res_next(&cur, XE_PAGE_SIZE), addr += XE_PAGE_SIZE) {
			ptes[count++] = pte + cur.start;
			if (count == VF2PF_UPDATE_GGTT_MAX_PTES) {
				u64 chunk_start = addr + XE_PAGE_SIZE - (u64)count * XE_PAGE_SIZE;

				ret = xe_ggtt_vf_explicit_send_ptes(ggtt, chunk_start, ptes, count);
				if (ret)
					return ret;
				count = 0;
			}
		}
	}

	if (!count)
		return 0;

	return xe_ggtt_vf_explicit_send_ptes(ggtt, addr - (u64)count * XE_PAGE_SIZE,
					     ptes, count);
}
#endif

static void xe_ggtt_clear(struct xe_ggtt *ggtt, u64 start, u64 size)
{
	u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[XE_CACHE_WB];
	u64 end = start + size - 1;
	u64 scratch_pte;

	xe_tile_assert(ggtt->tile, start < end);

	if (ggtt->scratch)
		scratch_pte = xe_bo_addr(ggtt->scratch, 0, XE_PAGE_SIZE) |
			      ggtt->pt_ops->pte_encode_flags(ggtt->scratch,
							     pat_index);
	else
		scratch_pte = 0;

#ifdef CONFIG_PCI_IOV
	if (xe_ggtt_vf_explicit_enabled(ggtt)) {
		int ret = xe_ggtt_vf_explicit_clear(ggtt, start, size, scratch_pte);

		if (!ret)
			return;

		xe_tile_warn(ggtt->tile,
			     "xe: MTL SR-IOV GGTT path: VF explicit clear failed (%pe), falling back to raw writes\n",
			     ERR_PTR(ret));
	}
#endif

	while (start < end) {
		ggtt->pt_ops->ggtt_set_pte(ggtt, start, scratch_pte);
		start += XE_PAGE_SIZE;
	}
}

static void primelockdep(struct xe_ggtt *ggtt)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&ggtt->lock);
	fs_reclaim_release(GFP_KERNEL);
}

/**
 * xe_ggtt_alloc - Allocate a GGTT for a given &xe_tile
 * @tile: &xe_tile
 *
 * Allocates a &xe_ggtt for a given tile.
 *
 * Return: &xe_ggtt on success, or NULL when out of memory.
 */
struct xe_ggtt *xe_ggtt_alloc(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_ggtt *ggtt;

	ggtt = drmm_kzalloc(&xe->drm, sizeof(*ggtt), GFP_KERNEL);
	if (!ggtt)
		return NULL;

	if (drmm_mutex_init(&xe->drm, &ggtt->lock))
		return NULL;

	primelockdep(ggtt);
	ggtt->tile = tile;

	return ggtt;
}

static void ggtt_fini_early(struct drm_device *drm, void *arg)
{
	struct xe_ggtt *ggtt = arg;

	destroy_workqueue(ggtt->wq);
	drm_mm_takedown(&ggtt->mm);
}

static void ggtt_fini(void *arg)
{
	struct xe_ggtt *ggtt = arg;

	ggtt->scratch = NULL;
}

#ifdef CONFIG_LOCKDEP
void xe_ggtt_might_lock(struct xe_ggtt *ggtt)
{
	might_lock(&ggtt->lock);
}
#endif

static const struct xe_ggtt_pt_ops xelp_pt_ops = {
	.pte_encode_flags = xelp_ggtt_pte_flags,
	.ggtt_set_pte = xe_ggtt_set_pte,
	.ggtt_get_pte = xe_ggtt_get_pte,
};

static const struct xe_ggtt_pt_ops xelpg_pt_ops = {
	.pte_encode_flags = xelpg_ggtt_pte_flags,
	.ggtt_set_pte = xe_ggtt_set_pte,
	.ggtt_get_pte = xe_ggtt_get_pte,
};

static const struct xe_ggtt_pt_ops xelpg_pt_wa_ops = {
	.pte_encode_flags = xelpg_ggtt_pte_flags,
	.ggtt_set_pte = xe_ggtt_set_pte_and_flush,
	.ggtt_get_pte = xe_ggtt_get_pte,
};

static void __xe_ggtt_init_early(struct xe_ggtt *ggtt, u32 reserved)
{
	drm_mm_init(&ggtt->mm, reserved,
		    ggtt->size - reserved);
}

int xe_ggtt_init_kunit(struct xe_ggtt *ggtt, u32 reserved, u32 size)
{
	ggtt->size = size;
	__xe_ggtt_init_early(ggtt, reserved);
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(xe_ggtt_init_kunit);

static void dev_fini_ggtt(void *arg)
{
	struct xe_ggtt *ggtt = arg;

	drain_workqueue(ggtt->wq);
}

/**
 * xe_ggtt_init_early - Early GGTT initialization
 * @ggtt: the &xe_ggtt to be initialized
 *
 * It allows to create new mappings usable by the GuC.
 * Mappings are not usable by the HW engines, as it doesn't have scratch nor
 * initial clear done to it yet. That will happen in the regular, non-early
 * GGTT initialization.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_init_early(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	unsigned int gsm_size;
	int err;

	if (IS_SRIOV_VF(xe) || GRAPHICS_VERx100(xe) >= 1250)
		gsm_size = SZ_8M; /* GGTT is expected to be 4GiB */
	else
		gsm_size = probe_gsm_size(pdev);

	if (gsm_size == 0) {
		xe_tile_err(ggtt->tile, "Hardware reported no preallocated GSM\n");
		return -ENOMEM;
	}

	ggtt->gsm = xe_ggtt_map_gsm(ggtt, gsm_size);
	if (!ggtt->gsm) {
		xe_tile_err(ggtt->tile, "Failed to map GGTT page table\n");
		return -ENOMEM;
	}
	ggtt->size = (gsm_size / 8) * (u64) XE_PAGE_SIZE;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		ggtt->flags |= XE_GGTT_FLAGS_64K;

	if (ggtt->size > GUC_GGTT_TOP)
		ggtt->size = GUC_GGTT_TOP;

	if (GRAPHICS_VERx100(xe) >= 1270)
		ggtt->pt_ops =
			(ggtt->tile->media_gt && XE_GT_WA(ggtt->tile->media_gt, 22019338487)) ||
			(ggtt->tile->primary_gt && XE_GT_WA(ggtt->tile->primary_gt, 22019338487)) ?
			&xelpg_pt_wa_ops : &xelpg_pt_ops;
	else
		ggtt->pt_ops = &xelp_pt_ops;

	ggtt->wq = alloc_workqueue("xe-ggtt-wq", WQ_MEM_RECLAIM, 0);
	if (!ggtt->wq)
		return -ENOMEM;
	INIT_WORK(&ggtt->invalidate_work, ggtt_invalidate_work_func);
	atomic_set(&ggtt->invalidate_pending, 0);

	__xe_ggtt_init_early(ggtt, xe_wopcm_size(xe));

	err = drmm_add_action_or_reset(&xe->drm, ggtt_fini_early, ggtt);
	if (err)
		return err;

	err = devm_add_action_or_reset(xe->drm.dev, dev_fini_ggtt, ggtt);
	if (err)
		return err;

	if (IS_SRIOV_VF(xe)) {
		err = xe_tile_sriov_vf_prepare_ggtt(ggtt->tile);
		if (err)
			return err;
	}

	return 0;
}
ALLOW_ERROR_INJECTION(xe_ggtt_init_early, ERRNO); /* See xe_pci_probe() */

static void xe_ggtt_invalidate(struct xe_ggtt *ggtt);

static void xe_ggtt_initial_clear(struct xe_ggtt *ggtt)
{
	struct drm_mm_node *hole;
	u64 start, end;

	/* Display may have allocated inside ggtt, so be careful with clearing here */
	mutex_lock(&ggtt->lock);
	drm_mm_for_each_hole(hole, &ggtt->mm, start, end)
		xe_ggtt_clear(ggtt, start, end - start);

	xe_ggtt_invalidate(ggtt);
	mutex_unlock(&ggtt->lock);
}

static void ggtt_node_remove(struct xe_ggtt_node *node)
{
	struct xe_ggtt *ggtt = node->ggtt;
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	bool bound;
	int idx;

	bound = drm_dev_enter(&xe->drm, &idx);

	mutex_lock(&ggtt->lock);
	if (bound)
		xe_ggtt_clear(ggtt, node->base.start, node->base.size);
	drm_mm_remove_node(&node->base);
	node->base.size = 0;
	mutex_unlock(&ggtt->lock);

	if (!bound)
		goto free_node;

	if (node->invalidate_on_remove)
		xe_ggtt_invalidate(ggtt);

	drm_dev_exit(idx);

free_node:
	xe_ggtt_node_fini(node);
}

static void ggtt_node_remove_work_func(struct work_struct *work)
{
	struct xe_ggtt_node *node = container_of(work, typeof(*node),
						 delayed_removal_work);
	struct xe_device *xe = tile_to_xe(node->ggtt->tile);

	guard(xe_pm_runtime)(xe);
	ggtt_node_remove(node);
}

/**
 * xe_ggtt_node_remove - Remove a &xe_ggtt_node from the GGTT
 * @node: the &xe_ggtt_node to be removed
 * @invalidate: if node needs invalidation upon removal
 */
void xe_ggtt_node_remove(struct xe_ggtt_node *node, bool invalidate)
{
	struct xe_ggtt *ggtt;
	struct xe_device *xe;

	if (!node || !node->ggtt)
		return;

	ggtt = node->ggtt;
	xe = tile_to_xe(ggtt->tile);

	node->invalidate_on_remove = invalidate;

	if (xe_pm_runtime_get_if_active(xe)) {
		ggtt_node_remove(node);
		xe_pm_runtime_put(xe);
	} else {
		queue_work(ggtt->wq, &node->delayed_removal_work);
	}
}

/**
 * xe_ggtt_init - Regular non-early GGTT initialization
 * @ggtt: the &xe_ggtt to be initialized
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_init(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	unsigned int flags;
	int err;

	/*
	 * So we don't need to worry about 64K GGTT layout when dealing with
	 * scratch entries, rather keep the scratch page in system memory on
	 * platforms where 64K pages are needed for VRAM.
	 */
	flags = 0;
	if (ggtt->flags & XE_GGTT_FLAGS_64K)
		flags |= XE_BO_FLAG_SYSTEM;
	else
		flags |= XE_BO_FLAG_VRAM_IF_DGFX(ggtt->tile);

	ggtt->scratch = xe_managed_bo_create_pin_map(xe, ggtt->tile, XE_PAGE_SIZE, flags);
	if (IS_ERR(ggtt->scratch)) {
		err = PTR_ERR(ggtt->scratch);
		goto err;
	}

	xe_map_memset(xe, &ggtt->scratch->vmap, 0, 0, xe_bo_size(ggtt->scratch));

	xe_ggtt_initial_clear(ggtt);

#ifdef CONFIG_PCI_IOV
	if (IS_SRIOV_VF(xe) && xe->sriov.vf.pf_version.major)
		ggtt->vf_relay_ready = true;
#endif

	return devm_add_action_or_reset(xe->drm.dev, ggtt_fini, ggtt);
err:
	ggtt->scratch = NULL;
	return err;
}

static void ggtt_invalidate_gt_tlb(struct xe_gt *gt)
{
	int err;

	if (!gt)
		return;

	err = xe_tlb_inval_ggtt(&gt->tlb_inval);
	xe_gt_WARN(gt, err, "Failed to invalidate GGTT (%pe)", ERR_PTR(err));
}

static void ggtt_invalidate_gt_tlb_mmio(struct xe_gt *gt)
{
	unsigned int fw_ref;

	if (!gt)
		return;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	xe_mmio_write32(&gt->mmio, GUC_TLB_INV_CR, GUC_TLB_INV_CR_INVALIDATE);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static void ggtt_invalidate_work_func(struct work_struct *work)
{
	struct xe_ggtt *ggtt = container_of(work, struct xe_ggtt, invalidate_work);
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	atomic_set(&ggtt->invalidate_pending, 0);
	xe_pm_runtime_get(xe);
	xe_ggtt_invalidate(ggtt);
	xe_pm_runtime_put(xe);

	if (atomic_xchg(&ggtt->invalidate_pending, 0) == 1)
		queue_work(ggtt->wq, &ggtt->invalidate_work);
}

static void xe_ggtt_invalidate_deferred(struct xe_ggtt *ggtt)
{
	if (atomic_xchg(&ggtt->invalidate_pending, 1) == 0)
		queue_work(ggtt->wq, &ggtt->invalidate_work);
}

static void xe_ggtt_invalidate(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	/*
	 * XXX: Barrier for GGTT pages. Unsure exactly why this required but
	 * without this LNL is having issues with the GuC reading scratch page
	 * vs. correct GGTT page. Not particularly a hot code path so blindly
	 * do a mmio read here which results in GuC reading correct GGTT page.
	 */
	xe_mmio_read32(xe_root_tile_mmio(xe), VF_CAP_REG);

	if (xe_ggtt_use_mtl_pf_mmio_invalidate(ggtt)) {
		drm_info_once(&xe->drm,
			      "xe: MTL SR-IOV GGTT path: PF uses i915-like direct MMIO GGTT invalidate primitive\n");
		ggtt_invalidate_gt_tlb_mmio(ggtt->tile->primary_gt);
		ggtt_invalidate_gt_tlb_mmio(ggtt->tile->media_gt);
		return;
	}

	/* Each GT in a tile has its own TLB to cache GGTT lookups */
	ggtt_invalidate_gt_tlb(ggtt->tile->primary_gt);
	ggtt_invalidate_gt_tlb(ggtt->tile->media_gt);
}

static void xe_ggtt_dump_node(struct xe_ggtt *ggtt,
			      const struct drm_mm_node *node, const char *description)
{
	char buf[10];

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		string_get_size(node->size, 1, STRING_UNITS_2, buf, sizeof(buf));
		xe_tile_dbg(ggtt->tile, "GGTT %#llx-%#llx (%s) %s\n",
			    node->start, node->start + node->size, buf, description);
	}
}

/**
 * xe_ggtt_node_insert_balloon_locked - prevent allocation of specified GGTT addresses
 * @node: the &xe_ggtt_node to hold reserved GGTT node
 * @start: the starting GGTT address of the reserved region
 * @end: then end GGTT address of the reserved region
 *
 * To be used in cases where ggtt->lock is already taken.
 * Use xe_ggtt_node_remove_balloon_locked() to release a reserved GGTT node.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_insert_balloon_locked(struct xe_ggtt_node *node, u64 start, u64 end)
{
	struct xe_ggtt *ggtt = node->ggtt;
	int err;

	xe_tile_assert(ggtt->tile, start < end);
	xe_tile_assert(ggtt->tile, IS_ALIGNED(start, XE_PAGE_SIZE));
	xe_tile_assert(ggtt->tile, IS_ALIGNED(end, XE_PAGE_SIZE));
	xe_tile_assert(ggtt->tile, !drm_mm_node_allocated(&node->base));
	lockdep_assert_held(&ggtt->lock);

	node->base.color = 0;
	node->base.start = start;
	node->base.size = end - start;

	err = drm_mm_reserve_node(&ggtt->mm, &node->base);

	if (xe_tile_WARN(ggtt->tile, err, "Failed to balloon GGTT %#llx-%#llx (%pe)\n",
			 node->base.start, node->base.start + node->base.size, ERR_PTR(err)))
		return err;

	xe_ggtt_dump_node(ggtt, &node->base, "balloon");
	return 0;
}

/**
 * xe_ggtt_node_remove_balloon_locked - release a reserved GGTT region
 * @node: the &xe_ggtt_node with reserved GGTT region
 *
 * To be used in cases where ggtt->lock is already taken.
 * See xe_ggtt_node_insert_balloon_locked() for details.
 */
void xe_ggtt_node_remove_balloon_locked(struct xe_ggtt_node *node)
{
	if (!xe_ggtt_node_allocated(node))
		return;

	lockdep_assert_held(&node->ggtt->lock);

	xe_ggtt_dump_node(node->ggtt, &node->base, "remove-balloon");

	drm_mm_remove_node(&node->base);
}

static void xe_ggtt_assert_fit(struct xe_ggtt *ggtt, u64 start, u64 size)
{
	struct xe_tile *tile = ggtt->tile;
	struct xe_device *xe = tile_to_xe(tile);
	u64 __maybe_unused wopcm = xe_wopcm_size(xe);

	xe_tile_assert(tile, start >= wopcm);
	xe_tile_assert(tile, start + size < ggtt->size - wopcm);
}

/**
 * xe_ggtt_shift_nodes_locked - Shift GGTT nodes to adjust for a change in usable address range.
 * @ggtt: the &xe_ggtt struct instance
 * @shift: change to the location of area provisioned for current VF
 *
 * This function moves all nodes from the GGTT VM, to a temp list. These nodes are expected
 * to represent allocations in range formerly assigned to current VF, before the range changed.
 * When the GGTT VM is completely clear of any nodes, they are re-added with shifted offsets.
 *
 * The function has no ability of failing - because it shifts existing nodes, without
 * any additional processing. If the nodes were successfully existing at the old address,
 * they will do the same at the new one. A fail inside this function would indicate that
 * the list of nodes was either already damaged, or that the shift brings the address range
 * outside of valid bounds. Both cases justify an assert rather than error code.
 */
void xe_ggtt_shift_nodes_locked(struct xe_ggtt *ggtt, s64 shift)
{
	struct xe_tile *tile __maybe_unused = ggtt->tile;
	struct drm_mm_node *node, *tmpn;
	LIST_HEAD(temp_list_head);

	lockdep_assert_held(&ggtt->lock);

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG))
		drm_mm_for_each_node_safe(node, tmpn, &ggtt->mm)
			xe_ggtt_assert_fit(ggtt, node->start + shift, node->size);

	drm_mm_for_each_node_safe(node, tmpn, &ggtt->mm) {
		drm_mm_remove_node(node);
		list_add(&node->node_list, &temp_list_head);
	}

	list_for_each_entry_safe(node, tmpn, &temp_list_head, node_list) {
		list_del(&node->node_list);
		node->start += shift;
		drm_mm_reserve_node(&ggtt->mm, node);
		xe_tile_assert(tile, drm_mm_node_allocated(node));
	}
}

/**
 * xe_ggtt_node_insert_locked - Locked version to insert a &xe_ggtt_node into the GGTT
 * @node: the &xe_ggtt_node to be inserted
 * @size: size of the node
 * @align: alignment constrain of the node
 * @mm_flags: flags to control the node behavior
 *
 * It cannot be called without first having called xe_ggtt_init() once.
 * To be used in cases where ggtt->lock is already taken.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_insert_locked(struct xe_ggtt_node *node,
			       u32 size, u32 align, u32 mm_flags)
{
	return drm_mm_insert_node_generic(&node->ggtt->mm, &node->base, size, align, 0,
					  mm_flags);
}

/**
 * xe_ggtt_node_insert - Insert a &xe_ggtt_node into the GGTT
 * @node: the &xe_ggtt_node to be inserted
 * @size: size of the node
 * @align: alignment constrain of the node
 *
 * It cannot be called without first having called xe_ggtt_init() once.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_insert(struct xe_ggtt_node *node, u32 size, u32 align)
{
	int ret;

	if (!node || !node->ggtt)
		return -ENOENT;

	mutex_lock(&node->ggtt->lock);
	ret = xe_ggtt_node_insert_locked(node, size, align,
					 DRM_MM_INSERT_HIGH);
	mutex_unlock(&node->ggtt->lock);

	return ret;
}

/**
 * xe_ggtt_node_init - Initialize %xe_ggtt_node struct
 * @ggtt: the &xe_ggtt where the new node will later be inserted/reserved.
 *
 * This function will allocate the struct %xe_ggtt_node and return its pointer.
 * This struct will then be freed after the node removal upon xe_ggtt_node_remove()
 * or xe_ggtt_node_remove_balloon_locked().
 * Having %xe_ggtt_node struct allocated doesn't mean that the node is already allocated
 * in GGTT. Only the xe_ggtt_node_insert(), xe_ggtt_node_insert_locked(),
 * xe_ggtt_node_insert_balloon_locked() will ensure the node is inserted or reserved in GGTT.
 *
 * Return: A pointer to %xe_ggtt_node struct on success. An ERR_PTR otherwise.
 **/
struct xe_ggtt_node *xe_ggtt_node_init(struct xe_ggtt *ggtt)
{
	struct xe_ggtt_node *node = kzalloc(sizeof(*node), GFP_NOFS);

	if (!node)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&node->delayed_removal_work, ggtt_node_remove_work_func);
#ifdef CONFIG_PCI_IOV
	INIT_WORK(&node->vf_apply_work, ggtt_vf_apply_work_func);
#endif
	node->ggtt = ggtt;

	return node;
}

/**
 * xe_ggtt_node_fini - Forcebly finalize %xe_ggtt_node struct
 * @node: the &xe_ggtt_node to be freed
 *
 * If anything went wrong with either xe_ggtt_node_insert(), xe_ggtt_node_insert_locked(),
 * or xe_ggtt_node_insert_balloon_locked(); and this @node is not going to be reused, then,
 * this function needs to be called to free the %xe_ggtt_node struct
 **/
void xe_ggtt_node_fini(struct xe_ggtt_node *node)
{
#ifdef CONFIG_PCI_IOV
	cancel_work_sync(&node->vf_apply_work);
#endif
	kfree(node);
}

/**
 * xe_ggtt_node_allocated - Check if node is allocated in GGTT
 * @node: the &xe_ggtt_node to be inspected
 *
 * Return: True if allocated, False otherwise.
 */
bool xe_ggtt_node_allocated(const struct xe_ggtt_node *node)
{
	if (!node || !node->ggtt)
		return false;

	return drm_mm_node_allocated(&node->base);
}

/**
 * xe_ggtt_node_pt_size() - Get the size of page table entries needed to map a GGTT node.
 * @node: the &xe_ggtt_node
 *
 * Return: GGTT node page table entries size in bytes.
 */
size_t xe_ggtt_node_pt_size(const struct xe_ggtt_node *node)
{
	if (!node)
		return 0;

	return node->base.size / XE_PAGE_SIZE * sizeof(u64);
}

/**
 * xe_ggtt_map_bo - Map the BO into GGTT
 * @ggtt: the &xe_ggtt where node will be mapped
 * @node: the &xe_ggtt_node where this BO is mapped
 * @bo: the &xe_bo to be mapped
 * @pat_index: Which pat_index to use.
 */
void xe_ggtt_map_bo(struct xe_ggtt *ggtt, struct xe_ggtt_node *node,
		    struct xe_bo *bo, u16 pat_index)
{

	u64 start, pte, end;
	struct xe_res_cursor cur;

	if (XE_WARN_ON(!node))
		return;

	start = node->base.start;
	end = start + xe_bo_size(bo);

	pte = ggtt->pt_ops->pte_encode_flags(bo, pat_index);
#ifdef CONFIG_PCI_IOV
	if (xe_ggtt_vf_explicit_enabled(ggtt)) {
		int ret = xe_ggtt_vf_explicit_map_bo(ggtt, start, bo, pte);

		if (!ret)
			return;

		xe_tile_warn(ggtt->tile,
			     "xe: MTL SR-IOV GGTT path: VF explicit map failed (%pe), falling back to raw writes\n",
			     ERR_PTR(ret));
	}
#endif
	if (!xe_bo_is_vram(bo) && !xe_bo_is_stolen(bo)) {
		xe_assert(xe_bo_device(bo), bo->ttm.ttm);

		for (xe_res_first_sg(xe_bo_sg(bo), 0, xe_bo_size(bo), &cur);
		     cur.remaining; xe_res_next(&cur, XE_PAGE_SIZE))
			ggtt->pt_ops->ggtt_set_pte(ggtt, end - cur.remaining,
						   pte | xe_res_dma(&cur));
	} else {
		/* Prepend GPU offset */
		pte |= vram_region_gpu_offset(bo->ttm.resource);

		for (xe_res_first(bo->ttm.resource, 0, xe_bo_size(bo), &cur);
		     cur.remaining; xe_res_next(&cur, XE_PAGE_SIZE))
			ggtt->pt_ops->ggtt_set_pte(ggtt, end - cur.remaining,
						   pte + cur.start);
	}
}

/**
 * xe_ggtt_map_bo_unlocked - Restore a mapping of a BO into GGTT
 * @ggtt: the &xe_ggtt where node will be mapped
 * @bo: the &xe_bo to be mapped
 *
 * This is used to restore a GGTT mapping after suspend.
 */
void xe_ggtt_map_bo_unlocked(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	u16 cache_mode = bo->flags & XE_BO_FLAG_NEEDS_UC ? XE_CACHE_NONE : XE_CACHE_WB;
	u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[cache_mode];

	mutex_lock(&ggtt->lock);
	xe_ggtt_map_bo(ggtt, bo->ggtt_node[ggtt->tile->id], bo, pat_index);
	mutex_unlock(&ggtt->lock);
}

static int __xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
				  u64 start, u64 end, struct drm_exec *exec)
{
	u64 alignment = bo->min_align > 0 ? bo->min_align : XE_PAGE_SIZE;
	u8 tile_id = ggtt->tile->id;
	int err;

	if (xe_bo_is_vram(bo) && ggtt->flags & XE_GGTT_FLAGS_64K)
		alignment = SZ_64K;

	if (XE_WARN_ON(bo->ggtt_node[tile_id])) {
		/* Someone's already inserted this BO in the GGTT */
		xe_tile_assert(ggtt->tile, bo->ggtt_node[tile_id]->base.size == xe_bo_size(bo));
		return 0;
	}

	err = xe_bo_validate(bo, NULL, false, exec);
	if (err)
		return err;

	xe_pm_runtime_get_noresume(tile_to_xe(ggtt->tile));

	bo->ggtt_node[tile_id] = xe_ggtt_node_init(ggtt);
	if (IS_ERR(bo->ggtt_node[tile_id])) {
		err = PTR_ERR(bo->ggtt_node[tile_id]);
		bo->ggtt_node[tile_id] = NULL;
		goto out;
	}

	mutex_lock(&ggtt->lock);
	err = drm_mm_insert_node_in_range(&ggtt->mm, &bo->ggtt_node[tile_id]->base,
					  xe_bo_size(bo), alignment, 0, start, end, 0);
	if (err) {
		xe_ggtt_node_fini(bo->ggtt_node[tile_id]);
		bo->ggtt_node[tile_id] = NULL;
	} else {
		u16 cache_mode = bo->flags & XE_BO_FLAG_NEEDS_UC ? XE_CACHE_NONE : XE_CACHE_WB;
		u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[cache_mode];

		xe_ggtt_map_bo(ggtt, bo->ggtt_node[tile_id], bo, pat_index);
	}
	mutex_unlock(&ggtt->lock);

	if (!err && bo->flags & XE_BO_FLAG_GGTT_INVALIDATE)
		xe_ggtt_invalidate(ggtt);

out:
	xe_pm_runtime_put(tile_to_xe(ggtt->tile));

	return err;
}

/**
 * xe_ggtt_insert_bo_at - Insert BO at a specific GGTT space
 * @ggtt: the &xe_ggtt where bo will be inserted
 * @bo: the &xe_bo to be inserted
 * @start: address where it will be inserted
 * @end: end of the range where it will be inserted
 * @exec: The drm_exec transaction to use for exhaustive eviction.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
			 u64 start, u64 end, struct drm_exec *exec)
{
	return __xe_ggtt_insert_bo_at(ggtt, bo, start, end, exec);
}

/**
 * xe_ggtt_insert_bo - Insert BO into GGTT
 * @ggtt: the &xe_ggtt where bo will be inserted
 * @bo: the &xe_bo to be inserted
 * @exec: The drm_exec transaction to use for exhaustive eviction.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo,
		      struct drm_exec *exec)
{
	return __xe_ggtt_insert_bo_at(ggtt, bo, 0, U64_MAX, exec);
}

/**
 * xe_ggtt_remove_bo - Remove a BO from the GGTT
 * @ggtt: the &xe_ggtt where node will be removed
 * @bo: the &xe_bo to be removed
 */
void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	u8 tile_id = ggtt->tile->id;

	if (XE_WARN_ON(!bo->ggtt_node[tile_id]))
		return;

	/* This BO is not currently in the GGTT */
	xe_tile_assert(ggtt->tile, bo->ggtt_node[tile_id]->base.size == xe_bo_size(bo));

	xe_ggtt_node_remove(bo->ggtt_node[tile_id],
			    bo->flags & XE_BO_FLAG_GGTT_INVALIDATE);
}

/**
 * xe_ggtt_largest_hole - Largest GGTT hole
 * @ggtt: the &xe_ggtt that will be inspected
 * @alignment: minimum alignment
 * @spare: If not NULL: in: desired memory size to be spared / out: Adjusted possible spare
 *
 * Return: size of the largest continuous GGTT region
 */
u64 xe_ggtt_largest_hole(struct xe_ggtt *ggtt, u64 alignment, u64 *spare)
{
	const struct drm_mm *mm = &ggtt->mm;
	const struct drm_mm_node *entry;
	u64 hole_min_start = xe_wopcm_size(tile_to_xe(ggtt->tile));
	u64 hole_start, hole_end, hole_size;
	u64 max_hole = 0;

	mutex_lock(&ggtt->lock);

	drm_mm_for_each_hole(entry, mm, hole_start, hole_end) {
		hole_start = max(hole_start, hole_min_start);
		hole_start = ALIGN(hole_start, alignment);
		hole_end = ALIGN_DOWN(hole_end, alignment);
		if (hole_start >= hole_end)
			continue;
		hole_size = hole_end - hole_start;
		if (spare)
			*spare -= min3(*spare, hole_size, max_hole);
		max_hole = max(max_hole, hole_size);
	}

	mutex_unlock(&ggtt->lock);

	return max_hole;
}

#ifdef CONFIG_PCI_IOV
static u64 xe_encode_vfid_pte(u16 vfid)
{
	return FIELD_PREP(GGTT_PTE_VFID, vfid) | XE_PAGE_PRESENT;
}

static u64 xe_ggtt_prepare_vf_pte(u64 pte, u16 vfid)
{
	return u64_replace_bits(pte, vfid, GGTT_PTE_VFID) | XE_PAGE_PRESENT;
}

static u64 xe_ggtt_write_one(struct xe_ggtt *ggtt, u64 addr, u64 pte, u16 vfid)
{
	ggtt->pt_ops->ggtt_set_pte(ggtt, addr, xe_ggtt_prepare_vf_pte(pte, vfid));
	return addr + XE_PAGE_SIZE;
}

static u64 xe_ggtt_write_dup_rep(struct xe_ggtt *ggtt, u64 addr, u64 pte, u16 vfid,
				 u16 num_entries, bool duplicated)
{
	u16 i;

	for (i = 0; i < num_entries; i++) {
		u64 entry = pte;

		if (!duplicated)
			entry += (u64)i * XE_PAGE_SIZE;

		addr = xe_ggtt_write_one(ggtt, addr, entry, vfid);
	}

	return addr;
}

static void xe_ggtt_reset_vf_shadow(struct xe_ggtt_node *node)
{
	if (node->vf_shadow_ptes)
		memset(node->vf_shadow_ptes, 0,
		       node->vf_shadow_len * sizeof(*node->vf_shadow_ptes));

	node->vf_apply_dirty = false;
	node->vf_apply_queued = false;
	node->vf_apply_start = 0;
	node->vf_apply_end = 0;
}

static void xe_ggtt_assign_locked(struct xe_ggtt *ggtt, struct xe_ggtt_node *node, u16 vfid)
{
	u64 start = node->base.start;
	u64 size = node->base.size;
	u64 end = start + size - 1;
	u64 pte = xe_encode_vfid_pte(vfid);

	lockdep_assert_held(&ggtt->lock);

	if (!drm_mm_node_allocated(&node->base))
		return;

	node->vfid = vfid;

	while (start < end) {
		ggtt->pt_ops->ggtt_set_pte(ggtt, start, pte);
		start += XE_PAGE_SIZE;
	}

	xe_ggtt_reset_vf_shadow(node);
	xe_ggtt_invalidate(ggtt);
}

void xe_ggtt_node_quiesce_vf_apply(struct xe_ggtt_node *node)
{
	if (!node)
		return;

	cancel_work_sync(&node->vf_apply_work);
}

/**
 * xe_ggtt_assign - assign a GGTT region to the VF
 * @node: the &xe_ggtt_node to update
 * @vfid: the VF identifier
 *
 * This function is used by the PF driver to assign a GGTT region to the VF.
 * In addition to PTE's VFID bits 11:2 also PRESENT bit 0 is set as on some
 * platforms VFs can't modify that either.
 */
void xe_ggtt_assign(struct xe_ggtt_node *node, u16 vfid)
{
	xe_ggtt_node_quiesce_vf_apply(node);
	mutex_lock(&node->ggtt->lock);
	xe_ggtt_assign_locked(node->ggtt, node, vfid);
	mutex_unlock(&node->ggtt->lock);
}

/**
 * xe_ggtt_node_save() - Save a &xe_ggtt_node to a buffer.
 * @node: the &xe_ggtt_node to be saved
 * @dst: destination buffer
 * @size: destination buffer size in bytes
 * @vfid: VF identifier
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_save(struct xe_ggtt_node *node, void *dst, size_t size, u16 vfid)
{
	struct xe_ggtt *ggtt;
	u64 start, end;
	u64 *buf = dst;
	u64 pte;

	if (!node)
		return -ENOENT;

	guard(mutex)(&node->ggtt->lock);

	if (xe_ggtt_node_pt_size(node) != size)
		return -EINVAL;

	ggtt = node->ggtt;
	start = node->base.start;
	end = start + node->base.size - 1;

	while (start < end) {
		pte = ggtt->pt_ops->ggtt_get_pte(ggtt, start);
		if (vfid != u64_get_bits(pte, GGTT_PTE_VFID))
			return -EPERM;

		*buf++ = u64_replace_bits(pte, 0, GGTT_PTE_VFID);
		start += XE_PAGE_SIZE;
	}

	return 0;
}

/**
 * xe_ggtt_node_load() - Load a &xe_ggtt_node from a buffer.
 * @node: the &xe_ggtt_node to be loaded
 * @src: source buffer
 * @size: source buffer size in bytes
 * @vfid: VF identifier
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_load(struct xe_ggtt_node *node, const void *src, size_t size, u16 vfid)
{
	u64 vfid_pte = xe_encode_vfid_pte(vfid);
	const u64 *buf = src;
	struct xe_ggtt *ggtt;
	u64 start, end;

	if (!node)
		return -ENOENT;

	guard(mutex)(&node->ggtt->lock);

	if (xe_ggtt_node_pt_size(node) != size)
		return -EINVAL;

	ggtt = node->ggtt;
	start = node->base.start;
	end = start + node->base.size - 1;

	while (start < end) {
		vfid_pte = u64_replace_bits(*buf++, vfid, GGTT_PTE_VFID);
		ggtt->pt_ops->ggtt_set_pte(ggtt, start, vfid_pte);
		start += XE_PAGE_SIZE;
	}
	xe_ggtt_invalidate(ggtt);

	return 0;
}

static void ggtt_vf_apply_work_func(struct work_struct *work)
{
	struct xe_ggtt_node *node = container_of(work, typeof(*node), vf_apply_work);
	struct xe_ggtt *ggtt = node->ggtt;
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	guard(xe_pm_runtime)(xe);

	for (;;) {
		u32 start, end, i;
		u64 ggtt_addr;

		mutex_lock(&ggtt->lock);
		if (!node->vf_apply_dirty || !node->vf_shadow_ptes ||
		    !xe_ggtt_node_allocated(node)) {
			node->vf_apply_queued = false;
			mutex_unlock(&ggtt->lock);
			break;
		}

		start = node->vf_apply_start;
		end = node->vf_apply_end;
		node->vf_apply_dirty = false;
		node->vf_apply_start = 0;
		node->vf_apply_end = 0;

		ggtt_addr = node->base.start + (u64)start * XE_PAGE_SIZE;
		for (i = start; i < end; i++, ggtt_addr += XE_PAGE_SIZE)
			ggtt->pt_ops->ggtt_set_pte(ggtt, ggtt_addr,
						   xe_ggtt_prepare_vf_pte(node->vf_shadow_ptes[i],
									 node->vfid));
		mutex_unlock(&ggtt->lock);

		xe_ggtt_invalidate_deferred(ggtt);
	}
}

int xe_ggtt_update_vf_ptes(struct xe_ggtt_node *node, u16 vfid, u32 pte_offset,
			   u8 mode, u16 num_copies, const u64 *ptes, u16 count)
{
	struct xe_ggtt *ggtt;
	struct xe_device *xe;
	u64 ggtt_addr, ggtt_addr_end, vf_ggtt_end;
	u64 entry;
	u16 n_ptes;
	u16 remaining;
	u16 copies;
	u16 i;
	bool duplicated;
	bool mtl_path;
	bool queue_apply = false;

	if (!node)
		return -ENOENT;
	if (!count)
		return -EINVAL;
	if (!xe_ggtt_node_allocated(node))
		return -ENOENT;

	ggtt = node->ggtt;
	xe = tile_to_xe(ggtt->tile);
	n_ptes = num_copies ? num_copies + count : count;
	ggtt_addr = node->base.start + (u64)pte_offset * XE_PAGE_SIZE;
	ggtt_addr_end = ggtt_addr + (u64)n_ptes * XE_PAGE_SIZE - 1;
	vf_ggtt_end = node->base.start + node->base.size - 1;
	if (ggtt_addr_end > vf_ggtt_end)
		return -ERANGE;

	if (node->vf_shadow_ptes && pte_offset + n_ptes > node->vf_shadow_len)
		return -ERANGE;

	mtl_path = xe_device_needs_mtl_ggtt_binder(xe) && IS_SRIOV_PF(xe);

	{
		guard(mutex)(&ggtt->lock);

		copies = num_copies + 1;
		remaining = count - 1;
		duplicated = mode == VF2PF_UPDATE_GGTT32_MODE_DUPLICATE ||
			     mode == VF2PF_UPDATE_GGTT32_MODE_DUPLICATE_LAST;

		switch (mode) {
		case VF2PF_UPDATE_GGTT32_MODE_DUPLICATE:
		case VF2PF_UPDATE_GGTT32_MODE_REPLICATE:
			for (i = 0; i < copies; i++) {
				entry = duplicated ? ptes[0] : ptes[0] + (u64)i * XE_PAGE_SIZE;
				if (node->vf_shadow_ptes)
					node->vf_shadow_ptes[pte_offset + i] = entry;
			}
			for (i = 0; i < remaining; i++) {
				entry = ptes[i + 1];
				if (node->vf_shadow_ptes)
					node->vf_shadow_ptes[pte_offset + copies + i] = entry;
			}
			if (!mtl_path || !node->vf_shadow_ptes) {
				ggtt_addr = xe_ggtt_write_dup_rep(ggtt, ggtt_addr, ptes[0], vfid,
								  copies, duplicated);
				for (i = 0; i < remaining; i++)
					ggtt_addr = xe_ggtt_write_one(ggtt, ggtt_addr,
								      ptes[i + 1], vfid);
			}
			break;
		case VF2PF_UPDATE_GGTT32_MODE_DUPLICATE_LAST:
		case VF2PF_UPDATE_GGTT32_MODE_REPLICATE_LAST:
			for (i = 0; i < remaining; i++) {
				entry = ptes[i];
				if (node->vf_shadow_ptes)
					node->vf_shadow_ptes[pte_offset + i] = entry;
			}
			for (i = 0; i < copies; i++) {
				entry = duplicated ? ptes[remaining] :
					ptes[remaining] + (u64)i * XE_PAGE_SIZE;
				if (node->vf_shadow_ptes)
					node->vf_shadow_ptes[pte_offset + remaining + i] = entry;
			}
			if (!mtl_path || !node->vf_shadow_ptes) {
				for (i = 0; i < remaining; i++)
					ggtt_addr = xe_ggtt_write_one(ggtt, ggtt_addr, ptes[i], vfid);
				ggtt_addr = xe_ggtt_write_dup_rep(ggtt, ggtt_addr, ptes[remaining],
								  vfid, copies, duplicated);
			}
			break;
		default:
			return -EINVAL;
		}

		if (mtl_path && node->vf_shadow_ptes) {
			u32 end = pte_offset + n_ptes;

			if (!node->vf_apply_dirty) {
				node->vf_apply_start = pte_offset;
				node->vf_apply_end = end;
				node->vf_apply_dirty = true;
			} else {
				node->vf_apply_start = min(node->vf_apply_start, pte_offset);
				node->vf_apply_end = max(node->vf_apply_end, end);
			}

			if (!node->vf_apply_queued) {
				node->vf_apply_queued = true;
				queue_apply = true;
			}
		}
	}

	if (mtl_path && node->vf_shadow_ptes) {
		if (queue_apply)
			queue_work(ggtt->wq, &node->vf_apply_work);
		return n_ptes;
	}

	xe_ggtt_invalidate_deferred(ggtt);
	return n_ptes;
}

#endif

/**
 * xe_ggtt_dump - Dump GGTT for debug
 * @ggtt: the &xe_ggtt to be dumped
 * @p: the &drm_mm_printer helper handle to be used to dump the information
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_dump(struct xe_ggtt *ggtt, struct drm_printer *p)
{
	int err;

	err = mutex_lock_interruptible(&ggtt->lock);
	if (err)
		return err;

	drm_mm_print(&ggtt->mm, p);
	mutex_unlock(&ggtt->lock);
	return err;
}

/**
 * xe_ggtt_print_holes - Print holes
 * @ggtt: the &xe_ggtt to be inspected
 * @alignment: min alignment
 * @p: the &drm_printer
 *
 * Print GGTT ranges that are available and return total size available.
 *
 * Return: Total available size.
 */
u64 xe_ggtt_print_holes(struct xe_ggtt *ggtt, u64 alignment, struct drm_printer *p)
{
	const struct drm_mm *mm = &ggtt->mm;
	const struct drm_mm_node *entry;
	u64 hole_min_start = xe_wopcm_size(tile_to_xe(ggtt->tile));
	u64 hole_start, hole_end, hole_size;
	u64 total = 0;
	char buf[10];

	mutex_lock(&ggtt->lock);

	drm_mm_for_each_hole(entry, mm, hole_start, hole_end) {
		hole_start = max(hole_start, hole_min_start);
		hole_start = ALIGN(hole_start, alignment);
		hole_end = ALIGN_DOWN(hole_end, alignment);
		if (hole_start >= hole_end)
			continue;
		hole_size = hole_end - hole_start;
		total += hole_size;

		string_get_size(hole_size, 1, STRING_UNITS_2, buf, sizeof(buf));
		drm_printf(p, "range:\t%#llx-%#llx\t(%s)\n",
			   hole_start, hole_end - 1, buf);
	}

	mutex_unlock(&ggtt->lock);

	return total;
}

/**
 * xe_ggtt_encode_pte_flags - Get PTE encoding flags for BO
 * @ggtt: &xe_ggtt
 * @bo: &xe_bo
 * @pat_index: The pat_index for the PTE.
 *
 * This function returns the pte_flags for a given BO, without  address.
 * It's used for DPT to fill a GGTT mapped BO with a linear lookup table.
 */
u64 xe_ggtt_encode_pte_flags(struct xe_ggtt *ggtt,
			     struct xe_bo *bo, u16 pat_index)
{
	return ggtt->pt_ops->pte_encode_flags(bo, pat_index);
}

/**
 * xe_ggtt_read_pte - Read a PTE from the GGTT
 * @ggtt: &xe_ggtt
 * @offset: the offset for which the mapping should be read.
 *
 * Used by testcases, and by display reading out an inherited bios FB.
 */
u64 xe_ggtt_read_pte(struct xe_ggtt *ggtt, u64 offset)
{
	return ioread64(ggtt->gsm + (offset / XE_PAGE_SIZE));
}
