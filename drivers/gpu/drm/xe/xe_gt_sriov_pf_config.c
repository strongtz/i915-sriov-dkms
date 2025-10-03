// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/string_choices.h>
#include <linux/wordpart.h>

#include "abi/guc_actions_sriov_abi.h"
#include "abi/guc_klvs_abi.h"

#include "regs/xe_guc_regs.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_policy.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc.h"
#include "xe_guc_buf.h"
#include "xe_guc_ct.h"
#include "xe_guc_db_mgr.h"
#include "xe_guc_fwif.h"
#include "xe_guc_id_mgr.h"
#include "xe_guc_klv_helpers.h"
#include "xe_guc_klv_thresholds_set.h"
#include "xe_guc_submit.h"
#include "xe_lmtt.h"
#include "xe_map.h"
#include "xe_migrate.h"
#include "xe_sriov.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_wopcm.h"

#define make_u64_from_u32(hi, lo) ((u64)((u64)(u32)(hi) << 32 | (u32)(lo)))

/*
 * Return: number of KLVs that were successfully parsed and saved,
 *         negative error code on failure.
 */
static int guc_action_update_vf_cfg(struct xe_guc *guc, u32 vfid,
				    u64 addr, u32 size)
{
	u32 request[] = {
		GUC_ACTION_PF2GUC_UPDATE_VF_CFG,
		vfid,
		lower_32_bits(addr),
		upper_32_bits(addr),
		size,
	};

	return xe_guc_ct_send_block(&guc->ct, request, ARRAY_SIZE(request));
}

/*
 * Return: 0 on success, negative error code on failure.
 */
static int pf_send_vf_cfg_reset(struct xe_gt *gt, u32 vfid)
{
	struct xe_guc *guc = &gt->uc.guc;
	int ret;

	ret = guc_action_update_vf_cfg(guc, vfid, 0, 0);

	return ret <= 0 ? ret : -EPROTO;
}

/*
 * Return: number of KLVs that were successfully parsed and saved,
 *         negative error code on failure.
 */
static int pf_send_vf_buf_klvs(struct xe_gt *gt, u32 vfid, struct xe_guc_buf buf, u32 num_dwords)
{
	struct xe_guc *guc = &gt->uc.guc;

	return guc_action_update_vf_cfg(guc, vfid, xe_guc_buf_flush(buf), num_dwords);
}

/*
 * Return: 0 on success, -ENOKEY if some KLVs were not updated, -EPROTO if reply was malformed,
 *         negative error code on failure.
 */
static int pf_push_vf_buf_klvs(struct xe_gt *gt, unsigned int vfid, u32 num_klvs,
			       struct xe_guc_buf buf, u32 num_dwords)
{
	int ret;

	ret = pf_send_vf_buf_klvs(gt, vfid, buf, num_dwords);

	if (ret != num_klvs) {
		int err = ret < 0 ? ret : ret < num_klvs ? -ENOKEY : -EPROTO;
		void *klvs = xe_guc_buf_cpu_ptr(buf);
		struct drm_printer p = xe_gt_info_printer(gt);
		char name[8];

		xe_gt_sriov_notice(gt, "Failed to push %s %u config KLV%s (%pe)\n",
				   xe_sriov_function_name(vfid, name, sizeof(name)),
				   num_klvs, str_plural(num_klvs), ERR_PTR(err));
		xe_guc_klv_print(klvs, num_dwords, &p);
		return err;
	}

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV)) {
		struct drm_printer p = xe_gt_dbg_printer(gt);
		void *klvs = xe_guc_buf_cpu_ptr(buf);
		char name[8];

		xe_gt_sriov_dbg(gt, "pushed %s config with %u KLV%s:\n",
				xe_sriov_function_name(vfid, name, sizeof(name)),
				num_klvs, str_plural(num_klvs));
		xe_guc_klv_print(klvs, num_dwords, &p);
	}

	return 0;
}

/*
 * Return: 0 on success, -ENOBUFS if no free buffer for the indirect data,
 *         negative error code on failure.
 */
static int pf_push_vf_cfg_klvs(struct xe_gt *gt, unsigned int vfid, u32 num_klvs,
			       const u32 *klvs, u32 num_dwords)
{
	CLASS(xe_guc_buf_from_data, buf)(&gt->uc.guc.buf, klvs, num_dwords * sizeof(u32));

	xe_gt_assert(gt, num_klvs == xe_guc_klv_count(klvs, num_dwords));

	if (!xe_guc_buf_is_valid(buf))
		return -ENOBUFS;

	return pf_push_vf_buf_klvs(gt, vfid, num_klvs, buf, num_dwords);
}

static int pf_push_vf_cfg_u32(struct xe_gt *gt, unsigned int vfid, u16 key, u32 value)
{
	u32 klv[] = {
		FIELD_PREP(GUC_KLV_0_KEY, key) | FIELD_PREP(GUC_KLV_0_LEN, 1),
		value,
	};

	return pf_push_vf_cfg_klvs(gt, vfid, 1, klv, ARRAY_SIZE(klv));
}

static int pf_push_vf_cfg_u64(struct xe_gt *gt, unsigned int vfid, u16 key, u64 value)
{
	u32 klv[] = {
		FIELD_PREP(GUC_KLV_0_KEY, key) | FIELD_PREP(GUC_KLV_0_LEN, 2),
		lower_32_bits(value),
		upper_32_bits(value),
	};

	return pf_push_vf_cfg_klvs(gt, vfid, 1, klv, ARRAY_SIZE(klv));
}

static int pf_push_vf_cfg_ggtt(struct xe_gt *gt, unsigned int vfid, u64 start, u64 size)
{
	u32 klvs[] = {
		PREP_GUC_KLV_TAG(VF_CFG_GGTT_START),
		lower_32_bits(start),
		upper_32_bits(start),
		PREP_GUC_KLV_TAG(VF_CFG_GGTT_SIZE),
		lower_32_bits(size),
		upper_32_bits(size),
	};

	return pf_push_vf_cfg_klvs(gt, vfid, 2, klvs, ARRAY_SIZE(klvs));
}

static int pf_push_vf_cfg_ctxs(struct xe_gt *gt, unsigned int vfid, u32 begin, u32 num)
{
	u32 klvs[] = {
		PREP_GUC_KLV_TAG(VF_CFG_BEGIN_CONTEXT_ID),
		begin,
		PREP_GUC_KLV_TAG(VF_CFG_NUM_CONTEXTS),
		num,
	};

	return pf_push_vf_cfg_klvs(gt, vfid, 2, klvs, ARRAY_SIZE(klvs));
}

static int pf_push_vf_cfg_dbs(struct xe_gt *gt, unsigned int vfid, u32 begin, u32 num)
{
	u32 klvs[] = {
		PREP_GUC_KLV_TAG(VF_CFG_BEGIN_DOORBELL_ID),
		begin,
		PREP_GUC_KLV_TAG(VF_CFG_NUM_DOORBELLS),
		num,
	};

	return pf_push_vf_cfg_klvs(gt, vfid, 2, klvs, ARRAY_SIZE(klvs));
}

static int pf_push_vf_cfg_exec_quantum(struct xe_gt *gt, unsigned int vfid, u32 *exec_quantum)
{
	/* GuC will silently clamp values exceeding max */
	*exec_quantum = min_t(u32, *exec_quantum, GUC_KLV_VF_CFG_EXEC_QUANTUM_MAX_VALUE);

	return pf_push_vf_cfg_u32(gt, vfid, GUC_KLV_VF_CFG_EXEC_QUANTUM_KEY, *exec_quantum);
}

static int pf_push_vf_cfg_preempt_timeout(struct xe_gt *gt, unsigned int vfid, u32 *preempt_timeout)
{
	/* GuC will silently clamp values exceeding max */
	*preempt_timeout = min_t(u32, *preempt_timeout, GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_MAX_VALUE);

	return pf_push_vf_cfg_u32(gt, vfid, GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_KEY, *preempt_timeout);
}

static int pf_push_vf_cfg_sched_priority(struct xe_gt *gt, unsigned int vfid, u32 priority)
{
	return pf_push_vf_cfg_u32(gt, vfid, GUC_KLV_VF_CFG_SCHED_PRIORITY_KEY, priority);
}

static int pf_push_vf_cfg_lmem(struct xe_gt *gt, unsigned int vfid, u64 size)
{
	return pf_push_vf_cfg_u64(gt, vfid, GUC_KLV_VF_CFG_LMEM_SIZE_KEY, size);
}

static int pf_push_vf_cfg_threshold(struct xe_gt *gt, unsigned int vfid,
				    enum xe_guc_klv_threshold_index index, u32 value)
{
	u32 key = xe_guc_klv_threshold_index_to_key(index);

	xe_gt_assert(gt, key);
	return pf_push_vf_cfg_u32(gt, vfid, key, value);
}

static struct xe_gt_sriov_config *pf_pick_vf_config(struct xe_gt *gt, unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	return &gt->sriov.pf.vfs[vfid].config;
}

/* Return: number of configuration dwords written */
static u32 encode_ggtt(u32 *cfg, u64 start, u64 size, bool details)
{
	u32 n = 0;

	if (details) {
		cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_GGTT_START);
		cfg[n++] = lower_32_bits(start);
		cfg[n++] = upper_32_bits(start);
	}

	cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_GGTT_SIZE);
	cfg[n++] = lower_32_bits(size);
	cfg[n++] = upper_32_bits(size);

	return n;
}

/* Return: number of configuration dwords written */
static u32 encode_config_ggtt(u32 *cfg, const struct xe_gt_sriov_config *config, bool details)
{
	struct xe_ggtt_node *node = config->ggtt_region;

	if (!xe_ggtt_node_allocated(node))
		return 0;

	return encode_ggtt(cfg, node->base.start, node->base.size, details);
}

/* Return: number of configuration dwords written */
static u32 encode_config(u32 *cfg, const struct xe_gt_sriov_config *config, bool details)
{
	u32 n = 0;

	n += encode_config_ggtt(cfg, config, details);

	if (details && config->num_ctxs) {
		cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_BEGIN_CONTEXT_ID);
		cfg[n++] = config->begin_ctx;
	}

	cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_NUM_CONTEXTS);
	cfg[n++] = config->num_ctxs;

	if (details && config->num_dbs) {
		cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_BEGIN_DOORBELL_ID);
		cfg[n++] = config->begin_db;
	}

	cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_NUM_DOORBELLS);
	cfg[n++] = config->num_dbs;

	if (config->lmem_obj) {
		cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_LMEM_SIZE);
		cfg[n++] = lower_32_bits(xe_bo_size(config->lmem_obj));
		cfg[n++] = upper_32_bits(xe_bo_size(config->lmem_obj));
	}

	cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_EXEC_QUANTUM);
	cfg[n++] = config->exec_quantum;

	cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_PREEMPT_TIMEOUT);
	cfg[n++] = config->preempt_timeout;

#define encode_threshold_config(TAG, ...) ({					\
	cfg[n++] = PREP_GUC_KLV_TAG(VF_CFG_THRESHOLD_##TAG);			\
	cfg[n++] = config->thresholds[MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG)];	\
});

	MAKE_XE_GUC_KLV_THRESHOLDS_SET(encode_threshold_config);
#undef encode_threshold_config

	return n;
}

static int pf_push_full_vf_config(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	u32 max_cfg_dwords = xe_guc_buf_cache_dwords(&gt->uc.guc.buf);
	CLASS(xe_guc_buf, buf)(&gt->uc.guc.buf, max_cfg_dwords);
	u32 num_dwords;
	int num_klvs;
	u32 *cfg;
	int err;

	if (!xe_guc_buf_is_valid(buf))
		return -ENOBUFS;

	cfg = xe_guc_buf_cpu_ptr(buf);
	num_dwords = encode_config(cfg, config, true);
	xe_gt_assert(gt, num_dwords <= max_cfg_dwords);

	if (xe_gt_is_media_type(gt)) {
		struct xe_gt *primary = gt->tile->primary_gt;
		struct xe_gt_sriov_config *other = pf_pick_vf_config(primary, vfid);

		/* media-GT will never include a GGTT config */
		xe_gt_assert(gt, !encode_config_ggtt(cfg + num_dwords, config, true));

		/* the GGTT config must be taken from the primary-GT instead */
		num_dwords += encode_config_ggtt(cfg + num_dwords, other, true);
	}
	xe_gt_assert(gt, num_dwords <= max_cfg_dwords);

	if (vfid == PFID) {
		u64 ggtt_start = xe_wopcm_size(gt_to_xe(gt));
		u64 ggtt_size = gt_to_tile(gt)->mem.ggtt->size - ggtt_start;

		/* plain PF config data will never include a real GGTT region */
		xe_gt_assert(gt, !encode_config_ggtt(cfg + num_dwords, config, true));

		/* fake PF GGTT config covers full GGTT range except reserved WOPCM */
		num_dwords += encode_ggtt(cfg + num_dwords, ggtt_start, ggtt_size, true);
	}

	num_klvs = xe_guc_klv_count(cfg, num_dwords);
	err = pf_push_vf_buf_klvs(gt, vfid, num_klvs, buf, num_dwords);

	return err;
}

static int pf_push_vf_cfg(struct xe_gt *gt, unsigned int vfid, bool reset)
{
	int err = 0;

	xe_gt_assert(gt, vfid);
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (reset)
		err = pf_send_vf_cfg_reset(gt, vfid);
	if (!err)
		err = pf_push_full_vf_config(gt, vfid);

	return err;
}

static int pf_refresh_vf_cfg(struct xe_gt *gt, unsigned int vfid)
{
	return pf_push_vf_cfg(gt, vfid, true);
}

static u64 pf_get_ggtt_alignment(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	return IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ? SZ_64K : SZ_4K;
}

static u64 pf_get_min_spare_ggtt(struct xe_gt *gt)
{
	/* XXX: preliminary */
	return IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV) ?
		pf_get_ggtt_alignment(gt) : SZ_64M;
}

static u64 pf_get_spare_ggtt(struct xe_gt *gt)
{
	u64 spare;

	xe_gt_assert(gt, xe_gt_is_main_type(gt));
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	spare = gt->sriov.pf.spare.ggtt_size;
	spare = max_t(u64, spare, pf_get_min_spare_ggtt(gt));

	return spare;
}

static int pf_set_spare_ggtt(struct xe_gt *gt, u64 size)
{
	xe_gt_assert(gt, xe_gt_is_main_type(gt));
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (size && size < pf_get_min_spare_ggtt(gt))
		return -EINVAL;

	size = round_up(size, pf_get_ggtt_alignment(gt));
	gt->sriov.pf.spare.ggtt_size = size;

	return 0;
}

static int pf_distribute_config_ggtt(struct xe_tile *tile, unsigned int vfid, u64 start, u64 size)
{
	int err, err2 = 0;

	err = pf_push_vf_cfg_ggtt(tile->primary_gt, vfid, start, size);

	if (tile->media_gt && !err)
		err2 = pf_push_vf_cfg_ggtt(tile->media_gt, vfid, start, size);

	return err ?: err2;
}

static void pf_release_ggtt(struct xe_tile *tile, struct xe_ggtt_node *node)
{
	if (xe_ggtt_node_allocated(node)) {
		/*
		 * explicit GGTT PTE assignment to the PF using xe_ggtt_assign()
		 * is redundant, as PTE will be implicitly re-assigned to PF by
		 * the xe_ggtt_clear() called by below xe_ggtt_remove_node().
		 */
		xe_ggtt_node_remove(node, false);
	} else {
		xe_ggtt_node_fini(node);
	}
}

static void pf_release_vf_config_ggtt(struct xe_gt *gt, struct xe_gt_sriov_config *config)
{
	pf_release_ggtt(gt_to_tile(gt), config->ggtt_region);
	config->ggtt_region = NULL;
}

static int pf_provision_vf_ggtt(struct xe_gt *gt, unsigned int vfid, u64 size)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	struct xe_ggtt_node *node;
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_ggtt *ggtt = tile->mem.ggtt;
	u64 alignment = pf_get_ggtt_alignment(gt);
	int err;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, xe_gt_is_main_type(gt));
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	size = round_up(size, alignment);

	if (xe_ggtt_node_allocated(config->ggtt_region)) {
		err = pf_distribute_config_ggtt(tile, vfid, 0, 0);
		if (unlikely(err))
			return err;

		pf_release_vf_config_ggtt(gt, config);

		err = pf_refresh_vf_cfg(gt, vfid);
		if (unlikely(err))
			return err;
	}
	xe_gt_assert(gt, !xe_ggtt_node_allocated(config->ggtt_region));

	if (!size)
		return 0;

	node = xe_ggtt_node_init(ggtt);
	if (IS_ERR(node))
		return PTR_ERR(node);

	err = xe_ggtt_node_insert(node, size, alignment);
	if (unlikely(err))
		goto err;

	xe_ggtt_assign(node, vfid);
	xe_gt_sriov_dbg_verbose(gt, "VF%u assigned GGTT %llx-%llx\n",
				vfid, node->base.start, node->base.start + node->base.size - 1);

	err = pf_distribute_config_ggtt(gt->tile, vfid, node->base.start, node->base.size);
	if (unlikely(err))
		goto err;

	config->ggtt_region = node;
	return 0;
err:
	pf_release_ggtt(tile, node);
	return err;
}

static u64 pf_get_vf_config_ggtt(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	struct xe_ggtt_node *node = config->ggtt_region;

	xe_gt_assert(gt, xe_gt_is_main_type(gt));
	return xe_ggtt_node_allocated(node) ? node->base.size : 0;
}

/**
 * xe_gt_sriov_pf_config_get_ggtt - Query size of GGTT address space of the VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 *
 * Return: size of the VF's assigned (or PF's spare) GGTT address space.
 */
u64 xe_gt_sriov_pf_config_get_ggtt(struct xe_gt *gt, unsigned int vfid)
{
	u64 size;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		size = pf_get_vf_config_ggtt(gt_to_tile(gt)->primary_gt, vfid);
	else
		size = pf_get_spare_ggtt(gt_to_tile(gt)->primary_gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return size;
}

static int pf_config_set_u64_done(struct xe_gt *gt, unsigned int vfid, u64 value,
				  u64 actual, const char *what, int err)
{
	char size[10];
	char name[8];

	xe_sriov_function_name(vfid, name, sizeof(name));

	if (unlikely(err)) {
		string_get_size(value, 1, STRING_UNITS_2, size, sizeof(size));
		xe_gt_sriov_notice(gt, "Failed to provision %s with %llu (%s) %s (%pe)\n",
				   name, value, size, what, ERR_PTR(err));
		string_get_size(actual, 1, STRING_UNITS_2, size, sizeof(size));
		xe_gt_sriov_info(gt, "%s provisioning remains at %llu (%s) %s\n",
				 name, actual, size, what);
		return err;
	}

	/* the actual value may have changed during provisioning */
	string_get_size(actual, 1, STRING_UNITS_2, size, sizeof(size));
	xe_gt_sriov_info(gt, "%s provisioned with %llu (%s) %s\n",
			 name, actual, size, what);
	return 0;
}

/**
 * xe_gt_sriov_pf_config_set_ggtt - Provision VF with GGTT space.
 * @gt: the &xe_gt (can't be media)
 * @vfid: the VF identifier
 * @size: requested GGTT size
 *
 * If &vfid represents PF, then function will change PF's spare GGTT config.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_ggtt(struct xe_gt *gt, unsigned int vfid, u64 size)
{
	int err;

	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		err = pf_provision_vf_ggtt(gt, vfid, size);
	else
		err = pf_set_spare_ggtt(gt, size);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u64_done(gt, vfid, size,
				      xe_gt_sriov_pf_config_get_ggtt(gt, vfid),
				      vfid ? "GGTT" : "spare GGTT", err);
}

static int pf_config_bulk_set_u64_done(struct xe_gt *gt, unsigned int first, unsigned int num_vfs,
				       u64 value, u64 (*get)(struct xe_gt*, unsigned int),
				       const char *what, unsigned int last, int err)
{
	char size[10];

	xe_gt_assert(gt, first);
	xe_gt_assert(gt, num_vfs);
	xe_gt_assert(gt, first <= last);

	if (num_vfs == 1)
		return pf_config_set_u64_done(gt, first, value, get(gt, first), what, err);

	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "Failed to bulk provision VF%u..VF%u with %s\n",
				   first, first + num_vfs - 1, what);
		if (last > first)
			pf_config_bulk_set_u64_done(gt, first, last - first, value,
						    get, what, last, 0);
		return pf_config_set_u64_done(gt, last, value, get(gt, last), what, err);
	}

	/* pick actual value from first VF - bulk provisioning shall be equal across all VFs */
	value = get(gt, first);
	string_get_size(value, 1, STRING_UNITS_2, size, sizeof(size));
	xe_gt_sriov_info(gt, "VF%u..VF%u provisioned with %llu (%s) %s\n",
			 first, first + num_vfs - 1, value, size, what);
	return 0;
}

/**
 * xe_gt_sriov_pf_config_bulk_set_ggtt - Provision many VFs with GGTT.
 * @gt: the &xe_gt (can't be media)
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision
 * @size: requested GGTT size
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_bulk_set_ggtt(struct xe_gt *gt, unsigned int vfid,
					unsigned int num_vfs, u64 size)
{
	unsigned int n;
	int err = 0;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	if (!num_vfs)
		return 0;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	for (n = vfid; n < vfid + num_vfs; n++) {
		err = pf_provision_vf_ggtt(gt, n, size);
		if (err)
			break;
	}
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_bulk_set_u64_done(gt, vfid, num_vfs, size,
					   xe_gt_sriov_pf_config_get_ggtt,
					   "GGTT", n, err);
}

/* Return: size of the largest continuous GGTT region */
static u64 pf_get_max_ggtt(struct xe_gt *gt)
{
	struct xe_ggtt *ggtt = gt_to_tile(gt)->mem.ggtt;
	u64 alignment = pf_get_ggtt_alignment(gt);
	u64 spare = pf_get_spare_ggtt(gt);
	u64 max_hole;

	max_hole = xe_ggtt_largest_hole(ggtt, alignment, &spare);

	xe_gt_sriov_dbg_verbose(gt, "HOLE max %lluK reserved %lluK\n",
				max_hole / SZ_1K, spare / SZ_1K);
	return max_hole > spare ? max_hole - spare : 0;
}

static u64 pf_estimate_fair_ggtt(struct xe_gt *gt, unsigned int num_vfs)
{
	u64 available = pf_get_max_ggtt(gt);
	u64 alignment = pf_get_ggtt_alignment(gt);
	u64 fair;

	/*
	 * To simplify the logic we only look at single largest GGTT region
	 * as that will be always the best fit for 1 VF case, and most likely
	 * will also nicely cover other cases where VFs are provisioned on the
	 * fresh and idle PF driver, without any stale GGTT allocations spread
	 * in the middle of the full GGTT range.
	 */

	fair = div_u64(available, num_vfs);
	fair = ALIGN_DOWN(fair, alignment);
	xe_gt_sriov_dbg_verbose(gt, "GGTT available(%lluK) fair(%u x %lluK)\n",
				available / SZ_1K, num_vfs, fair / SZ_1K);
	return fair;
}

/**
 * xe_gt_sriov_pf_config_set_fair_ggtt - Provision many VFs with fair GGTT.
 * @gt: the &xe_gt (can't be media)
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_fair_ggtt(struct xe_gt *gt, unsigned int vfid,
					unsigned int num_vfs)
{
	u64 fair;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, num_vfs);
	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	fair = pf_estimate_fair_ggtt(gt, num_vfs);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (!fair)
		return -ENOSPC;

	return xe_gt_sriov_pf_config_bulk_set_ggtt(gt, vfid, num_vfs, fair);
}

static u32 pf_get_min_spare_ctxs(struct xe_gt *gt)
{
	/* XXX: preliminary */
	return IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV) ?
		hweight64(gt->info.engine_mask) : SZ_256;
}

static u32 pf_get_spare_ctxs(struct xe_gt *gt)
{
	u32 spare;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	spare = gt->sriov.pf.spare.num_ctxs;
	spare = max_t(u32, spare, pf_get_min_spare_ctxs(gt));

	return spare;
}

static int pf_set_spare_ctxs(struct xe_gt *gt, u32 spare)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (spare > GUC_ID_MAX)
		return -EINVAL;

	if (spare && spare < pf_get_min_spare_ctxs(gt))
		return -EINVAL;

	gt->sriov.pf.spare.num_ctxs = spare;

	return 0;
}

/* Return: start ID or negative error code on failure */
static int pf_reserve_ctxs(struct xe_gt *gt, u32 num)
{
	struct xe_guc_id_mgr *idm = &gt->uc.guc.submission_state.idm;
	unsigned int spare = pf_get_spare_ctxs(gt);

	return xe_guc_id_mgr_reserve(idm, num, spare);
}

static void pf_release_ctxs(struct xe_gt *gt, u32 start, u32 num)
{
	struct xe_guc_id_mgr *idm = &gt->uc.guc.submission_state.idm;

	if (num)
		xe_guc_id_mgr_release(idm, start, num);
}

static void pf_release_config_ctxs(struct xe_gt *gt, struct xe_gt_sriov_config *config)
{
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	pf_release_ctxs(gt, config->begin_ctx, config->num_ctxs);
	config->begin_ctx = 0;
	config->num_ctxs = 0;
}

static int pf_provision_vf_ctxs(struct xe_gt *gt, unsigned int vfid, u32 num_ctxs)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	int ret;

	xe_gt_assert(gt, vfid);

	if (num_ctxs > GUC_ID_MAX)
		return -EINVAL;

	if (config->num_ctxs) {
		ret = pf_push_vf_cfg_ctxs(gt, vfid, 0, 0);
		if (unlikely(ret))
			return ret;

		pf_release_config_ctxs(gt, config);

		ret = pf_refresh_vf_cfg(gt, vfid);
		if (unlikely(ret))
			return ret;
	}

	if (!num_ctxs)
		return 0;

	ret = pf_reserve_ctxs(gt, num_ctxs);
	if (unlikely(ret < 0))
		return ret;

	config->begin_ctx = ret;
	config->num_ctxs = num_ctxs;

	ret = pf_push_vf_cfg_ctxs(gt, vfid, config->begin_ctx, config->num_ctxs);
	if (unlikely(ret)) {
		pf_release_config_ctxs(gt, config);
		return ret;
	}

	xe_gt_sriov_dbg_verbose(gt, "VF%u contexts %u-%u\n",
				vfid, config->begin_ctx, config->begin_ctx + config->num_ctxs - 1);
	return 0;
}

static u32 pf_get_vf_config_ctxs(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);

	return config->num_ctxs;
}

/**
 * xe_gt_sriov_pf_config_get_ctxs - Get VF's GuC contexts IDs quota.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 * If &vfid represents a PF then number of PF's spare GuC context IDs is returned.
 *
 * Return: VF's quota (or PF's spare).
 */
u32 xe_gt_sriov_pf_config_get_ctxs(struct xe_gt *gt, unsigned int vfid)
{
	u32 num_ctxs;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		num_ctxs = pf_get_vf_config_ctxs(gt, vfid);
	else
		num_ctxs = pf_get_spare_ctxs(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return num_ctxs;
}

static const char *no_unit(u32 unused)
{
	return "";
}

static const char *spare_unit(u32 unused)
{
	return " spare";
}

static int pf_config_set_u32_done(struct xe_gt *gt, unsigned int vfid, u32 value, u32 actual,
				  const char *what, const char *(*unit)(u32), int err)
{
	char name[8];

	xe_sriov_function_name(vfid, name, sizeof(name));

	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "Failed to provision %s with %u%s %s (%pe)\n",
				   name, value, unit(value), what, ERR_PTR(err));
		xe_gt_sriov_info(gt, "%s provisioning remains at %u%s %s\n",
				 name, actual, unit(actual), what);
		return err;
	}

	/* the actual value may have changed during provisioning */
	xe_gt_sriov_info(gt, "%s provisioned with %u%s %s\n",
			 name, actual, unit(actual), what);
	return 0;
}

/**
 * xe_gt_sriov_pf_config_set_ctxs - Configure GuC contexts IDs quota for the VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @num_ctxs: requested number of GuC contexts IDs (0 to release)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_ctxs(struct xe_gt *gt, unsigned int vfid, u32 num_ctxs)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		err = pf_provision_vf_ctxs(gt, vfid, num_ctxs);
	else
		err = pf_set_spare_ctxs(gt, num_ctxs);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u32_done(gt, vfid, num_ctxs,
				      xe_gt_sriov_pf_config_get_ctxs(gt, vfid),
				      "GuC context IDs", vfid ? no_unit : spare_unit, err);
}

static int pf_config_bulk_set_u32_done(struct xe_gt *gt, unsigned int first, unsigned int num_vfs,
				       u32 value, u32 (*get)(struct xe_gt*, unsigned int),
				       const char *what, const char *(*unit)(u32),
				       unsigned int last, int err)
{
	xe_gt_assert(gt, first);
	xe_gt_assert(gt, num_vfs);
	xe_gt_assert(gt, first <= last);

	if (num_vfs == 1)
		return pf_config_set_u32_done(gt, first, value, get(gt, first), what, unit, err);

	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "Failed to bulk provision VF%u..VF%u with %s\n",
				   first, first + num_vfs - 1, what);
		if (last > first)
			pf_config_bulk_set_u32_done(gt, first, last - first, value,
						    get, what, unit, last, 0);
		return pf_config_set_u32_done(gt, last, value, get(gt, last), what, unit, err);
	}

	/* pick actual value from first VF - bulk provisioning shall be equal across all VFs */
	value = get(gt, first);
	xe_gt_sriov_info(gt, "VF%u..VF%u provisioned with %u%s %s\n",
			 first, first + num_vfs - 1, value, unit(value), what);
	return 0;
}

/**
 * xe_gt_sriov_pf_config_bulk_set_ctxs - Provision many VFs with GuC context IDs.
 * @gt: the &xe_gt
 * @vfid: starting VF identifier
 * @num_vfs: number of VFs to provision
 * @num_ctxs: requested number of GuC contexts IDs (0 to release)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_bulk_set_ctxs(struct xe_gt *gt, unsigned int vfid,
					unsigned int num_vfs, u32 num_ctxs)
{
	unsigned int n;
	int err = 0;

	xe_gt_assert(gt, vfid);

	if (!num_vfs)
		return 0;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	for (n = vfid; n < vfid + num_vfs; n++) {
		err = pf_provision_vf_ctxs(gt, n, num_ctxs);
		if (err)
			break;
	}
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_bulk_set_u32_done(gt, vfid, num_vfs, num_ctxs,
					   xe_gt_sriov_pf_config_get_ctxs,
					   "GuC context IDs", no_unit, n, err);
}

static u32 pf_estimate_fair_ctxs(struct xe_gt *gt, unsigned int num_vfs)
{
	struct xe_guc_id_mgr *idm = &gt->uc.guc.submission_state.idm;
	u32 spare = pf_get_spare_ctxs(gt);
	u32 fair = (idm->total - spare) / num_vfs;
	int ret;

	for (; fair; --fair) {
		ret = xe_guc_id_mgr_reserve(idm, fair * num_vfs, spare);
		if (ret < 0)
			continue;
		xe_guc_id_mgr_release(idm, ret, fair * num_vfs);
		break;
	}

	xe_gt_sriov_dbg_verbose(gt, "contexts fair(%u x %u)\n", num_vfs, fair);
	return fair;
}

/**
 * xe_gt_sriov_pf_config_set_fair_ctxs - Provision many VFs with fair GuC context IDs.
 * @gt: the &xe_gt
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision (can't be 0)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_fair_ctxs(struct xe_gt *gt, unsigned int vfid,
					unsigned int num_vfs)
{
	u32 fair;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, num_vfs);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	fair = pf_estimate_fair_ctxs(gt, num_vfs);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (!fair)
		return -ENOSPC;

	return xe_gt_sriov_pf_config_bulk_set_ctxs(gt, vfid, num_vfs, fair);
}

static u32 pf_get_min_spare_dbs(struct xe_gt *gt)
{
	/* XXX: preliminary, we don't use doorbells yet! */
	return IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV) ? 1 : 0;
}

static u32 pf_get_spare_dbs(struct xe_gt *gt)
{
	u32 spare;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	spare = gt->sriov.pf.spare.num_dbs;
	spare = max_t(u32, spare, pf_get_min_spare_dbs(gt));

	return spare;
}

static int pf_set_spare_dbs(struct xe_gt *gt, u32 spare)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (spare > GUC_NUM_DOORBELLS)
		return -EINVAL;

	if (spare && spare < pf_get_min_spare_dbs(gt))
		return -EINVAL;

	gt->sriov.pf.spare.num_dbs = spare;
	return 0;
}

/* Return: start ID or negative error code on failure */
static int pf_reserve_dbs(struct xe_gt *gt, u32 num)
{
	struct xe_guc_db_mgr *dbm = &gt->uc.guc.dbm;
	unsigned int spare = pf_get_spare_dbs(gt);

	return xe_guc_db_mgr_reserve_range(dbm, num, spare);
}

static void pf_release_dbs(struct xe_gt *gt, u32 start, u32 num)
{
	struct xe_guc_db_mgr *dbm = &gt->uc.guc.dbm;

	if (num)
		xe_guc_db_mgr_release_range(dbm, start, num);
}

static void pf_release_config_dbs(struct xe_gt *gt, struct xe_gt_sriov_config *config)
{
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	pf_release_dbs(gt, config->begin_db, config->num_dbs);
	config->begin_db = 0;
	config->num_dbs = 0;
}

static int pf_provision_vf_dbs(struct xe_gt *gt, unsigned int vfid, u32 num_dbs)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	int ret;

	xe_gt_assert(gt, vfid);

	if (num_dbs > GUC_NUM_DOORBELLS)
		return -EINVAL;

	if (config->num_dbs) {
		ret = pf_push_vf_cfg_dbs(gt, vfid, 0, 0);
		if (unlikely(ret))
			return ret;

		pf_release_config_dbs(gt, config);

		ret = pf_refresh_vf_cfg(gt, vfid);
		if (unlikely(ret))
			return ret;
	}

	if (!num_dbs)
		return 0;

	ret = pf_reserve_dbs(gt, num_dbs);
	if (unlikely(ret < 0))
		return ret;

	config->begin_db = ret;
	config->num_dbs = num_dbs;

	ret = pf_push_vf_cfg_dbs(gt, vfid, config->begin_db, config->num_dbs);
	if (unlikely(ret)) {
		pf_release_config_dbs(gt, config);
		return ret;
	}

	xe_gt_sriov_dbg_verbose(gt, "VF%u doorbells %u-%u\n",
				vfid, config->begin_db, config->begin_db + config->num_dbs - 1);
	return 0;
}

static u32 pf_get_vf_config_dbs(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);

	return config->num_dbs;
}

/**
 * xe_gt_sriov_pf_config_get_dbs - Get VF's GuC doorbells IDs quota.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 * If &vfid represents a PF then number of PF's spare GuC doorbells IDs is returned.
 *
 * Return: VF's quota (or PF's spare).
 */
u32 xe_gt_sriov_pf_config_get_dbs(struct xe_gt *gt, unsigned int vfid)
{
	u32 num_dbs;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		num_dbs = pf_get_vf_config_dbs(gt, vfid);
	else
		num_dbs = pf_get_spare_dbs(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return num_dbs;
}

/**
 * xe_gt_sriov_pf_config_set_dbs - Configure GuC doorbells IDs quota for the VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @num_dbs: requested number of GuC doorbells IDs (0 to release)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_dbs(struct xe_gt *gt, unsigned int vfid, u32 num_dbs)
{
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid <= xe_sriov_pf_get_totalvfs(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		err = pf_provision_vf_dbs(gt, vfid, num_dbs);
	else
		err = pf_set_spare_dbs(gt, num_dbs);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u32_done(gt, vfid, num_dbs,
				      xe_gt_sriov_pf_config_get_dbs(gt, vfid),
				      "GuC doorbell IDs", vfid ? no_unit : spare_unit, err);
}

/**
 * xe_gt_sriov_pf_config_bulk_set_dbs - Provision many VFs with GuC context IDs.
 * @gt: the &xe_gt
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision
 * @num_dbs: requested number of GuC doorbell IDs (0 to release)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_bulk_set_dbs(struct xe_gt *gt, unsigned int vfid,
				       unsigned int num_vfs, u32 num_dbs)
{
	unsigned int n;
	int err = 0;

	xe_gt_assert(gt, vfid);

	if (!num_vfs)
		return 0;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	for (n = vfid; n < vfid + num_vfs; n++) {
		err = pf_provision_vf_dbs(gt, n, num_dbs);
		if (err)
			break;
	}
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_bulk_set_u32_done(gt, vfid, num_vfs, num_dbs,
					   xe_gt_sriov_pf_config_get_dbs,
					   "GuC doorbell IDs", no_unit, n, err);
}

static u32 pf_estimate_fair_dbs(struct xe_gt *gt, unsigned int num_vfs)
{
	struct xe_guc_db_mgr *dbm = &gt->uc.guc.dbm;
	u32 spare = pf_get_spare_dbs(gt);
	u32 fair = (GUC_NUM_DOORBELLS - spare) / num_vfs;
	int ret;

	for (; fair; --fair) {
		ret = xe_guc_db_mgr_reserve_range(dbm, fair * num_vfs, spare);
		if (ret < 0)
			continue;
		xe_guc_db_mgr_release_range(dbm, ret, fair * num_vfs);
		break;
	}

	xe_gt_sriov_dbg_verbose(gt, "doorbells fair(%u x %u)\n", num_vfs, fair);
	return fair;
}

/**
 * xe_gt_sriov_pf_config_set_fair_dbs - Provision many VFs with fair GuC doorbell  IDs.
 * @gt: the &xe_gt
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision (can't be 0)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_fair_dbs(struct xe_gt *gt, unsigned int vfid,
				       unsigned int num_vfs)
{
	u32 fair;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, num_vfs);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	fair = pf_estimate_fair_dbs(gt, num_vfs);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (!fair)
		return -ENOSPC;

	return xe_gt_sriov_pf_config_bulk_set_dbs(gt, vfid, num_vfs, fair);
}

static u64 pf_get_lmem_alignment(struct xe_gt *gt)
{
	/* this might be platform dependent */
	return SZ_2M;
}

static u64 pf_get_min_spare_lmem(struct xe_gt *gt)
{
	/* this might be platform dependent */
	return SZ_128M; /* XXX: preliminary */
}

static u64 pf_get_spare_lmem(struct xe_gt *gt)
{
	u64 spare;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	spare = gt->sriov.pf.spare.lmem_size;
	spare = max_t(u64, spare, pf_get_min_spare_lmem(gt));

	return spare;
}

static int pf_set_spare_lmem(struct xe_gt *gt, u64 size)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (size && size < pf_get_min_spare_lmem(gt))
		return -EINVAL;

	gt->sriov.pf.spare.lmem_size = size;
	return 0;
}

static u64 pf_get_vf_config_lmem(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	struct xe_bo *bo;

	bo = config->lmem_obj;
	return bo ? xe_bo_size(bo) : 0;
}

static int pf_distribute_config_lmem(struct xe_gt *gt, unsigned int vfid, u64 size)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tile *tile;
	unsigned int tid;
	int err;

	for_each_tile(tile, xe, tid) {
		if (tile->primary_gt == gt) {
			err = pf_push_vf_cfg_lmem(gt, vfid, size);
		} else {
			u64 lmem = pf_get_vf_config_lmem(tile->primary_gt, vfid);

			if (!lmem)
				continue;
			err = pf_push_vf_cfg_lmem(gt, vfid, lmem);
		}
		if (unlikely(err))
			return err;
	}
	return 0;
}

static void pf_force_lmtt_invalidate(struct xe_device *xe)
{
	struct xe_lmtt *lmtt;
	struct xe_tile *tile;
	unsigned int tid;

	xe_assert(xe, xe_device_has_lmtt(xe));
	xe_assert(xe, IS_SRIOV_PF(xe));

	for_each_tile(tile, xe, tid) {
		lmtt = &tile->sriov.pf.lmtt;
		xe_lmtt_invalidate_hw(lmtt);
	}
}

static void pf_reset_vf_lmtt(struct xe_device *xe, unsigned int vfid)
{
	struct xe_lmtt *lmtt;
	struct xe_tile *tile;
	unsigned int tid;

	xe_assert(xe, xe_device_has_lmtt(xe));
	xe_assert(xe, IS_SRIOV_PF(xe));

	for_each_tile(tile, xe, tid) {
		lmtt = &tile->sriov.pf.lmtt;
		xe_lmtt_drop_pages(lmtt, vfid);
	}
}

static int pf_update_vf_lmtt(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt_sriov_config *config;
	struct xe_tile *tile;
	struct xe_lmtt *lmtt;
	struct xe_bo *bo;
	struct xe_gt *gt;
	u64 total, offset;
	unsigned int gtid;
	unsigned int tid;
	int err;

	xe_assert(xe, xe_device_has_lmtt(xe));
	xe_assert(xe, IS_SRIOV_PF(xe));

	total = 0;
	for_each_tile(tile, xe, tid)
		total += pf_get_vf_config_lmem(tile->primary_gt, vfid);

	for_each_tile(tile, xe, tid) {
		lmtt = &tile->sriov.pf.lmtt;

		xe_lmtt_drop_pages(lmtt, vfid);
		if (!total)
			continue;

		err  = xe_lmtt_prepare_pages(lmtt, vfid, total);
		if (err)
			goto fail;

		offset = 0;
		for_each_gt(gt, xe, gtid) {
			if (xe_gt_is_media_type(gt))
				continue;

			config = pf_pick_vf_config(gt, vfid);
			bo = config->lmem_obj;
			if (!bo)
				continue;

			err = xe_lmtt_populate_pages(lmtt, vfid, bo, offset);
			if (err)
				goto fail;
			offset += xe_bo_size(bo);
		}
	}

	pf_force_lmtt_invalidate(xe);
	return 0;

fail:
	for_each_tile(tile, xe, tid) {
		lmtt = &tile->sriov.pf.lmtt;
		xe_lmtt_drop_pages(lmtt, vfid);
	}
	return err;
}

static void pf_release_vf_config_lmem(struct xe_gt *gt, struct xe_gt_sriov_config *config)
{
	xe_gt_assert(gt, IS_DGFX(gt_to_xe(gt)));
	xe_gt_assert(gt, xe_gt_is_main_type(gt));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (config->lmem_obj) {
		xe_bo_unpin_map_no_vm(config->lmem_obj);
		config->lmem_obj = NULL;
	}
}

static int pf_provision_vf_lmem(struct xe_gt *gt, unsigned int vfid, u64 size)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bo *bo;
	int err;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, IS_DGFX(xe));
	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	size = round_up(size, pf_get_lmem_alignment(gt));

	if (config->lmem_obj) {
		err = pf_distribute_config_lmem(gt, vfid, 0);
		if (unlikely(err))
			return err;

		if (xe_device_has_lmtt(xe))
			pf_reset_vf_lmtt(xe, vfid);
		pf_release_vf_config_lmem(gt, config);
	}
	xe_gt_assert(gt, !config->lmem_obj);

	if (!size)
		return 0;

	xe_gt_assert(gt, pf_get_lmem_alignment(gt) == SZ_2M);
	bo = xe_bo_create_locked(xe, tile, NULL,
				 ALIGN(size, PAGE_SIZE),
				 ttm_bo_type_kernel,
				 XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				 XE_BO_FLAG_NEEDS_2M |
				 XE_BO_FLAG_PINNED |
				 XE_BO_FLAG_PINNED_LATE_RESTORE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = xe_bo_pin(bo);
	xe_bo_unlock(bo);
	if (unlikely(err)) {
		xe_bo_put(bo);
		return err;
	}

	config->lmem_obj = bo;

	if (xe_device_has_lmtt(xe)) {
		err = pf_update_vf_lmtt(xe, vfid);
		if (unlikely(err))
			goto release;
	}

	err = pf_push_vf_cfg_lmem(gt, vfid, xe_bo_size(bo));
	if (unlikely(err))
		goto reset_lmtt;

	xe_gt_sriov_dbg_verbose(gt, "VF%u LMEM %zu (%zuM)\n",
				vfid, xe_bo_size(bo), xe_bo_size(bo) / SZ_1M);
	return 0;

reset_lmtt:
	if (xe_device_has_lmtt(xe))
		pf_reset_vf_lmtt(xe, vfid);
release:
	pf_release_vf_config_lmem(gt, config);
	return err;
}

/**
 * xe_gt_sriov_pf_config_get_lmem - Get VF's LMEM quota.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 *
 * Return: VF's (or PF's spare) LMEM quota.
 */
u64 xe_gt_sriov_pf_config_get_lmem(struct xe_gt *gt, unsigned int vfid)
{
	u64 size;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		size = pf_get_vf_config_lmem(gt, vfid);
	else
		size = pf_get_spare_lmem(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return size;
}

/**
 * xe_gt_sriov_pf_config_set_lmem - Provision VF with LMEM.
 * @gt: the &xe_gt (can't be media)
 * @vfid: the VF identifier
 * @size: requested LMEM size
 *
 * This function can only be called on PF.
 */
int xe_gt_sriov_pf_config_set_lmem(struct xe_gt *gt, unsigned int vfid, u64 size)
{
	int err;

	xe_gt_assert(gt, xe_device_has_lmtt(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (vfid)
		err = pf_provision_vf_lmem(gt, vfid, size);
	else
		err = pf_set_spare_lmem(gt, size);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u64_done(gt, vfid, size,
				      xe_gt_sriov_pf_config_get_lmem(gt, vfid),
				      vfid ? "LMEM" : "spare LMEM", err);
}

/**
 * xe_gt_sriov_pf_config_bulk_set_lmem - Provision many VFs with LMEM.
 * @gt: the &xe_gt (can't be media)
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision
 * @size: requested LMEM size
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_bulk_set_lmem(struct xe_gt *gt, unsigned int vfid,
					unsigned int num_vfs, u64 size)
{
	unsigned int n;
	int err = 0;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	if (!num_vfs)
		return 0;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	for (n = vfid; n < vfid + num_vfs; n++) {
		err = pf_provision_vf_lmem(gt, n, size);
		if (err)
			break;
	}
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_bulk_set_u64_done(gt, vfid, num_vfs, size,
					   xe_gt_sriov_pf_config_get_lmem,
					   "LMEM", n, err);
}

static u64 pf_query_free_lmem(struct xe_gt *gt)
{
	struct xe_tile *tile = gt->tile;

	return xe_ttm_vram_get_avail(&tile->mem.vram.ttm.manager);
}

static u64 pf_query_max_lmem(struct xe_gt *gt)
{
	u64 alignment = pf_get_lmem_alignment(gt);
	u64 spare = pf_get_spare_lmem(gt);
	u64 free = pf_query_free_lmem(gt);
	u64 avail;

	/* XXX: need to account for 2MB blocks only */
	avail = free > spare ? free - spare : 0;
	avail = round_down(avail, alignment);

	return avail;
}

#ifdef CONFIG_DRM_XE_DEBUG_SRIOV
#define MAX_FAIR_LMEM	SZ_128M	/* XXX: make it small for the driver bringup */
#endif

static u64 pf_estimate_fair_lmem(struct xe_gt *gt, unsigned int num_vfs)
{
	u64 available = pf_query_max_lmem(gt);
	u64 alignment = pf_get_lmem_alignment(gt);
	u64 fair;

	fair = div_u64(available, num_vfs);
	fair = ALIGN_DOWN(fair, alignment);
#ifdef MAX_FAIR_LMEM
	fair = min_t(u64, MAX_FAIR_LMEM, fair);
#endif
	xe_gt_sriov_dbg_verbose(gt, "LMEM available(%lluM) fair(%u x %lluM)\n",
				available / SZ_1M, num_vfs, fair / SZ_1M);
	return fair;
}

/**
 * xe_gt_sriov_pf_config_set_fair_lmem - Provision many VFs with fair LMEM.
 * @gt: the &xe_gt (can't be media)
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision (can't be 0)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_fair_lmem(struct xe_gt *gt, unsigned int vfid,
					unsigned int num_vfs)
{
	u64 fair;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, num_vfs);
	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	if (!xe_device_has_lmtt(gt_to_xe(gt)))
		return 0;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	fair = pf_estimate_fair_lmem(gt, num_vfs);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (!fair)
		return -ENOSPC;

	return xe_gt_sriov_pf_config_bulk_set_lmem(gt, vfid, num_vfs, fair);
}

/**
 * xe_gt_sriov_pf_config_set_fair - Provision many VFs with fair resources.
 * @gt: the &xe_gt
 * @vfid: starting VF identifier (can't be 0)
 * @num_vfs: number of VFs to provision (can't be 0)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_fair(struct xe_gt *gt, unsigned int vfid,
				   unsigned int num_vfs)
{
	int result = 0;
	int err;

	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, num_vfs);

	if (xe_gt_is_main_type(gt)) {
		err = xe_gt_sriov_pf_config_set_fair_ggtt(gt, vfid, num_vfs);
		result = result ?: err;
		err = xe_gt_sriov_pf_config_set_fair_lmem(gt, vfid, num_vfs);
		result = result ?: err;
	}
	err = xe_gt_sriov_pf_config_set_fair_ctxs(gt, vfid, num_vfs);
	result = result ?: err;
	err = xe_gt_sriov_pf_config_set_fair_dbs(gt, vfid, num_vfs);
	result = result ?: err;

	return result;
}

static const char *exec_quantum_unit(u32 exec_quantum)
{
	return exec_quantum ? "ms" : "(infinity)";
}

static int pf_provision_exec_quantum(struct xe_gt *gt, unsigned int vfid,
				     u32 exec_quantum)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	int err;

	err = pf_push_vf_cfg_exec_quantum(gt, vfid, &exec_quantum);
	if (unlikely(err))
		return err;

	config->exec_quantum = exec_quantum;
	return 0;
}

static int pf_get_exec_quantum(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);

	return config->exec_quantum;
}

/**
 * xe_gt_sriov_pf_config_set_exec_quantum - Configure execution quantum for the VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @exec_quantum: requested execution quantum in milliseconds (0 is infinity)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_exec_quantum(struct xe_gt *gt, unsigned int vfid,
					   u32 exec_quantum)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_exec_quantum(gt, vfid, exec_quantum);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u32_done(gt, vfid, exec_quantum,
				      xe_gt_sriov_pf_config_get_exec_quantum(gt, vfid),
				      "execution quantum", exec_quantum_unit, err);
}

/**
 * xe_gt_sriov_pf_config_get_exec_quantum - Get VF's execution quantum.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 *
 * Return: VF's (or PF's) execution quantum in milliseconds.
 */
u32 xe_gt_sriov_pf_config_get_exec_quantum(struct xe_gt *gt, unsigned int vfid)
{
	u32 exec_quantum;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	exec_quantum = pf_get_exec_quantum(gt, vfid);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return exec_quantum;
}

static const char *preempt_timeout_unit(u32 preempt_timeout)
{
	return preempt_timeout ? "us" : "(infinity)";
}

static int pf_provision_preempt_timeout(struct xe_gt *gt, unsigned int vfid,
					u32 preempt_timeout)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	int err;

	err = pf_push_vf_cfg_preempt_timeout(gt, vfid, &preempt_timeout);
	if (unlikely(err))
		return err;

	config->preempt_timeout = preempt_timeout;

	return 0;
}

static int pf_get_preempt_timeout(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);

	return config->preempt_timeout;
}

/**
 * xe_gt_sriov_pf_config_set_preempt_timeout - Configure preemption timeout for the VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @preempt_timeout: requested preemption timeout in microseconds (0 is infinity)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_preempt_timeout(struct xe_gt *gt, unsigned int vfid,
					      u32 preempt_timeout)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_preempt_timeout(gt, vfid, preempt_timeout);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u32_done(gt, vfid, preempt_timeout,
				      xe_gt_sriov_pf_config_get_preempt_timeout(gt, vfid),
				      "preemption timeout", preempt_timeout_unit, err);
}

/**
 * xe_gt_sriov_pf_config_get_preempt_timeout - Get VF's preemption timeout.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 *
 * Return: VF's (or PF's) preemption timeout in microseconds.
 */
u32 xe_gt_sriov_pf_config_get_preempt_timeout(struct xe_gt *gt, unsigned int vfid)
{
	u32 preempt_timeout;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	preempt_timeout = pf_get_preempt_timeout(gt, vfid);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return preempt_timeout;
}

static const char *sched_priority_unit(u32 priority)
{
	return priority == GUC_SCHED_PRIORITY_LOW ? "(low)" :
		priority == GUC_SCHED_PRIORITY_NORMAL ? "(normal)" :
		priority == GUC_SCHED_PRIORITY_HIGH ? "(high)" :
		"(?)";
}

static int pf_provision_sched_priority(struct xe_gt *gt, unsigned int vfid, u32 priority)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	int err;

	err = pf_push_vf_cfg_sched_priority(gt, vfid, priority);
	if (unlikely(err))
		return err;

	config->sched_priority = priority;
	return 0;
}

static int pf_get_sched_priority(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);

	return config->sched_priority;
}

/**
 * xe_gt_sriov_pf_config_set_sched_priority() - Configure scheduling priority.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @priority: requested scheduling priority
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_sched_priority(struct xe_gt *gt, unsigned int vfid, u32 priority)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_sched_priority(gt, vfid, priority);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u32_done(gt, vfid, priority,
				      xe_gt_sriov_pf_config_get_sched_priority(gt, vfid),
				      "scheduling priority", sched_priority_unit, err);
}

/**
 * xe_gt_sriov_pf_config_get_sched_priority - Get VF's scheduling priority.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 *
 * Return: VF's (or PF's) scheduling priority.
 */
u32 xe_gt_sriov_pf_config_get_sched_priority(struct xe_gt *gt, unsigned int vfid)
{
	u32 priority;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	priority = pf_get_sched_priority(gt, vfid);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return priority;
}

static void pf_reset_config_sched(struct xe_gt *gt, struct xe_gt_sriov_config *config)
{
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	config->exec_quantum = 0;
	config->preempt_timeout = 0;
}

static int pf_provision_threshold(struct xe_gt *gt, unsigned int vfid,
				  enum xe_guc_klv_threshold_index index, u32 value)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	int err;

	err = pf_push_vf_cfg_threshold(gt, vfid, index, value);
	if (unlikely(err))
		return err;

	config->thresholds[index] = value;

	return 0;
}

static int pf_get_threshold(struct xe_gt *gt, unsigned int vfid,
			    enum xe_guc_klv_threshold_index index)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);

	return config->thresholds[index];
}

static const char *threshold_unit(u32 threshold)
{
	return threshold ? "" : "(disabled)";
}

/**
 * xe_gt_sriov_pf_config_set_threshold - Configure threshold for the VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @index: the threshold index
 * @value: requested value (0 means disabled)
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_set_threshold(struct xe_gt *gt, unsigned int vfid,
					enum xe_guc_klv_threshold_index index, u32 value)
{
	u32 key = xe_guc_klv_threshold_index_to_key(index);
	const char *name = xe_guc_klv_key_to_string(key);
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_threshold(gt, vfid, index, value);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return pf_config_set_u32_done(gt, vfid, value,
				      xe_gt_sriov_pf_config_get_threshold(gt, vfid, index),
				      name, threshold_unit, err);
}

/**
 * xe_gt_sriov_pf_config_get_threshold - Get VF's threshold.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 * @index: the threshold index
 *
 * This function can only be called on PF.
 *
 * Return: value of VF's (or PF's) threshold.
 */
u32 xe_gt_sriov_pf_config_get_threshold(struct xe_gt *gt, unsigned int vfid,
					enum xe_guc_klv_threshold_index index)
{
	u32 value;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	value = pf_get_threshold(gt, vfid, index);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return value;
}

static void pf_reset_config_thresholds(struct xe_gt *gt, struct xe_gt_sriov_config *config)
{
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

#define reset_threshold_config(TAG, ...) ({				\
	config->thresholds[MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG)] = 0;	\
});

	MAKE_XE_GUC_KLV_THRESHOLDS_SET(reset_threshold_config);
#undef reset_threshold_config
}

static void pf_release_vf_config(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	struct xe_device *xe = gt_to_xe(gt);

	if (xe_gt_is_main_type(gt)) {
		pf_release_vf_config_ggtt(gt, config);
		if (IS_DGFX(xe)) {
			pf_release_vf_config_lmem(gt, config);
			if (xe_device_has_lmtt(xe))
				pf_update_vf_lmtt(xe, vfid);
		}
	}
	pf_release_config_ctxs(gt, config);
	pf_release_config_dbs(gt, config);
	pf_reset_config_sched(gt, config);
	pf_reset_config_thresholds(gt, config);
}

/**
 * xe_gt_sriov_pf_config_release - Release and reset VF configuration.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be PF)
 * @force: force configuration release
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_release(struct xe_gt *gt, unsigned int vfid, bool force)
{
	int err;

	xe_gt_assert(gt, vfid);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_send_vf_cfg_reset(gt, vfid);
	if (!err || force)
		pf_release_vf_config(gt, vfid);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "VF%u unprovisioning failed with error (%pe)%s\n",
				   vfid, ERR_PTR(err),
				   force ? " but all resources were released anyway!" : "");
	}

	return force ? 0 : err;
}

static void pf_sanitize_ggtt(struct xe_ggtt_node *ggtt_region, unsigned int vfid)
{
	if (xe_ggtt_node_allocated(ggtt_region))
		xe_ggtt_assign(ggtt_region, vfid);
}

static int pf_sanitize_lmem(struct xe_tile *tile, struct xe_bo *bo, long timeout)
{
	struct xe_migrate *m = tile->migrate;
	struct dma_fence *fence;
	int err;

	if (!bo)
		return 0;

	xe_bo_lock(bo, false);
	fence = xe_migrate_clear(m, bo, bo->ttm.resource, XE_MIGRATE_CLEAR_FLAG_FULL);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
	} else if (!fence) {
		err = -ENOMEM;
	} else {
		long ret = dma_fence_wait_timeout(fence, false, timeout);

		err = ret > 0 ? 0 : ret < 0 ? ret : -ETIMEDOUT;
		dma_fence_put(fence);
		if (!err)
			xe_gt_sriov_dbg_verbose(tile->primary_gt, "LMEM cleared in %dms\n",
						jiffies_to_msecs(timeout - ret));
	}
	xe_bo_unlock(bo);

	return err;
}

static int pf_sanitize_vf_resources(struct xe_gt *gt, u32 vfid, long timeout)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, vfid);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	int err = 0;

	/*
	 * Only GGTT and LMEM requires to be cleared by the PF.
	 * GuC doorbell IDs and context IDs do not need any clearing.
	 */
	if (xe_gt_is_main_type(gt)) {
		pf_sanitize_ggtt(config->ggtt_region, vfid);
		if (IS_DGFX(xe))
			err = pf_sanitize_lmem(tile, config->lmem_obj, timeout);
	}

	return err;
}

/**
 * xe_gt_sriov_pf_config_sanitize() - Sanitize VF's resources.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be PF)
 * @timeout: maximum timeout to wait for completion in jiffies
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_sanitize(struct xe_gt *gt, unsigned int vfid, long timeout)
{
	int err;

	xe_gt_assert(gt, vfid != PFID);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_sanitize_vf_resources(gt, vfid, timeout);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (unlikely(err))
		xe_gt_sriov_notice(gt, "VF%u resource sanitizing failed (%pe)\n",
				   vfid, ERR_PTR(err));
	return err;
}

/**
 * xe_gt_sriov_pf_config_push - Reprovision VF's configuration.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be PF)
 * @refresh: explicit refresh
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_push(struct xe_gt *gt, unsigned int vfid, bool refresh)
{
	int err = 0;

	xe_gt_assert(gt, vfid);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_push_vf_cfg(gt, vfid, refresh);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "Failed to %s VF%u configuration (%pe)\n",
				   refresh ? "refresh" : "push", vfid, ERR_PTR(err));
	}

	return err;
}

static int pf_validate_vf_config(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt *primary_gt = gt_to_tile(gt)->primary_gt;
	struct xe_device *xe = gt_to_xe(gt);
	bool is_primary = xe_gt_is_main_type(gt);
	bool valid_ggtt, valid_ctxs, valid_dbs;
	bool valid_any, valid_all;

	valid_ggtt = pf_get_vf_config_ggtt(primary_gt, vfid);
	valid_ctxs = pf_get_vf_config_ctxs(gt, vfid);
	valid_dbs = pf_get_vf_config_dbs(gt, vfid);

	/* note that GuC doorbells are optional */
	valid_any = valid_ctxs || valid_dbs;
	valid_all = valid_ctxs;

	/* and GGTT/LMEM is configured on primary GT only */
	valid_all = valid_all && valid_ggtt;
	valid_any = valid_any || (valid_ggtt && is_primary);

	if (xe_device_has_lmtt(xe)) {
		bool valid_lmem = pf_get_vf_config_lmem(primary_gt, vfid);

		valid_any = valid_any || (valid_lmem && is_primary);
		valid_all = valid_all && valid_lmem;
	}

	return valid_all ? 0 : valid_any ? -ENOKEY : -ENODATA;
}

/**
 * xe_gt_sriov_pf_config_is_empty - Check VF's configuration.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be PF)
 *
 * This function can only be called on PF.
 *
 * Return: true if VF mandatory configuration (GGTT, LMEM, ...) is empty.
 */
bool xe_gt_sriov_pf_config_is_empty(struct xe_gt *gt, unsigned int vfid)
{
	bool empty;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	empty = pf_validate_vf_config(gt, vfid) == -ENODATA;
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return empty;
}

/**
 * xe_gt_sriov_pf_config_save - Save a VF provisioning config as binary blob.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be PF)
 * @buf: the buffer to save a config to (or NULL if query the buf size)
 * @size: the size of the buffer (or 0 if query the buf size)
 *
 * This function can only be called on PF.
 *
 * Return: minimum size of the buffer or the number of bytes saved,
 *         or a negative error code on failure.
 */
ssize_t xe_gt_sriov_pf_config_save(struct xe_gt *gt, unsigned int vfid, void *buf, size_t size)
{
	struct xe_gt_sriov_config *config;
	ssize_t ret;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid);
	xe_gt_assert(gt, !(!buf ^ !size));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	ret = pf_validate_vf_config(gt, vfid);
	if (!size) {
		ret = ret ? 0 : SZ_4K;
	} else if (!ret) {
		if (size < SZ_4K) {
			ret = -ENOBUFS;
		} else {
			config = pf_pick_vf_config(gt, vfid);
			ret = encode_config(buf, config, false) * sizeof(u32);
		}
	}
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return ret;
}

static int pf_restore_vf_config_klv(struct xe_gt *gt, unsigned int vfid,
				    u32 key, u32 len, const u32 *value)
{
	switch (key) {
	case GUC_KLV_VF_CFG_NUM_CONTEXTS_KEY:
		if (len != GUC_KLV_VF_CFG_NUM_CONTEXTS_LEN)
			return -EBADMSG;
		return pf_provision_vf_ctxs(gt, vfid, value[0]);

	case GUC_KLV_VF_CFG_NUM_DOORBELLS_KEY:
		if (len != GUC_KLV_VF_CFG_NUM_DOORBELLS_LEN)
			return -EBADMSG;
		return pf_provision_vf_dbs(gt, vfid, value[0]);

	case GUC_KLV_VF_CFG_EXEC_QUANTUM_KEY:
		if (len != GUC_KLV_VF_CFG_EXEC_QUANTUM_LEN)
			return -EBADMSG;
		return pf_provision_exec_quantum(gt, vfid, value[0]);

	case GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_KEY:
		if (len != GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_LEN)
			return -EBADMSG;
		return pf_provision_preempt_timeout(gt, vfid, value[0]);

	/* auto-generate case statements */
#define define_threshold_key_to_provision_case(TAG, ...)				\
	case MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY(TAG):					\
		BUILD_BUG_ON(MAKE_GUC_KLV_VF_CFG_THRESHOLD_LEN(TAG) != 1u);		\
		if (len != MAKE_GUC_KLV_VF_CFG_THRESHOLD_LEN(TAG))			\
			return -EBADMSG;						\
		return pf_provision_threshold(gt, vfid,					\
					      MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG),	\
					      value[0]);

	MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_threshold_key_to_provision_case)
#undef define_threshold_key_to_provision_case
	}

	if (xe_gt_is_media_type(gt))
		return -EKEYREJECTED;

	switch (key) {
	case GUC_KLV_VF_CFG_GGTT_SIZE_KEY:
		if (len != GUC_KLV_VF_CFG_GGTT_SIZE_LEN)
			return -EBADMSG;
		return pf_provision_vf_ggtt(gt, vfid, make_u64_from_u32(value[1], value[0]));

	case GUC_KLV_VF_CFG_LMEM_SIZE_KEY:
		if (!IS_DGFX(gt_to_xe(gt)))
			return -EKEYREJECTED;
		if (len != GUC_KLV_VF_CFG_LMEM_SIZE_LEN)
			return -EBADMSG;
		return pf_provision_vf_lmem(gt, vfid, make_u64_from_u32(value[1], value[0]));
	}

	return -EKEYREJECTED;
}

static int pf_restore_vf_config(struct xe_gt *gt, unsigned int vfid,
				const u32 *klvs, size_t num_dwords)
{
	int err;

	while (num_dwords >= GUC_KLV_LEN_MIN) {
		u32 key = FIELD_GET(GUC_KLV_0_KEY, klvs[0]);
		u32 len = FIELD_GET(GUC_KLV_0_LEN, klvs[0]);

		klvs += GUC_KLV_LEN_MIN;
		num_dwords -= GUC_KLV_LEN_MIN;

		if (num_dwords < len)
			err = -EBADMSG;
		else
			err = pf_restore_vf_config_klv(gt, vfid, key, len, klvs);

		if (err) {
			xe_gt_sriov_dbg(gt, "restore failed on key %#x (%pe)\n", key, ERR_PTR(err));
			return err;
		}

		klvs += len;
		num_dwords -= len;
	}

	return pf_validate_vf_config(gt, vfid);
}

/**
 * xe_gt_sriov_pf_config_restore - Restore a VF provisioning config from binary blob.
 * @gt: the &xe_gt
 * @vfid: the VF identifier (can't be PF)
 * @buf: the buffer with config data
 * @size: the size of the config data
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_restore(struct xe_gt *gt, unsigned int vfid,
				  const void *buf, size_t size)
{
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid);

	if (!size)
		return -ENODATA;

	if (size % sizeof(u32))
		return -EINVAL;

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV)) {
		struct drm_printer p = xe_gt_dbg_printer(gt);

		drm_printf(&p, "restoring VF%u config:\n", vfid);
		xe_guc_klv_print(buf, size / sizeof(u32), &p);
	}

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_send_vf_cfg_reset(gt, vfid);
	if (!err) {
		pf_release_vf_config(gt, vfid);
		err = pf_restore_vf_config(gt, vfid, buf, size / sizeof(u32));
	}
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return err;
}

static void pf_prepare_self_config(struct xe_gt *gt)
{
	struct xe_gt_sriov_config *config = pf_pick_vf_config(gt, PFID);

	/*
	 * We want PF to be allowed to use all of context ID, doorbells IDs
	 * and whole usable GGTT area. While we can store ctxs/dbs numbers
	 * directly in the config structure, can't do the same with the GGTT
	 * configuration, so let it be prepared on demand while pushing KLVs.
	 */
	config->num_ctxs = GUC_ID_MAX;
	config->num_dbs = GUC_NUM_DOORBELLS;
}

static int pf_push_self_config(struct xe_gt *gt)
{
	int err;

	err = pf_push_full_vf_config(gt, PFID);
	if (err) {
		xe_gt_sriov_err(gt, "Failed to push self configuration (%pe)\n",
				ERR_PTR(err));
		return err;
	}

	xe_gt_sriov_dbg_verbose(gt, "self configuration completed\n");
	return 0;
}

static void fini_config(void *arg)
{
	struct xe_gt *gt = arg;
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(xe);

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	for (n = 1; n <= total_vfs; n++)
		pf_release_vf_config(gt, n);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));
}

/**
 * xe_gt_sriov_pf_config_init - Initialize SR-IOV configuration data.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	pf_prepare_self_config(gt);
	err = pf_push_self_config(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, fini_config, gt);
}

/**
 * xe_gt_sriov_pf_config_restart - Restart SR-IOV configurations after a GT reset.
 * @gt: the &xe_gt
 *
 * Any prior configurations pushed to GuC are lost when the GT is reset.
 * Push again all non-empty VF configurations to the GuC.
 *
 * This function can only be called on PF.
 */
void xe_gt_sriov_pf_config_restart(struct xe_gt *gt)
{
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(gt_to_xe(gt));
	unsigned int fail = 0, skip = 0;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	pf_push_self_config(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	for (n = 1; n <= total_vfs; n++) {
		if (xe_gt_sriov_pf_config_is_empty(gt, n))
			skip++;
		else if (xe_gt_sriov_pf_config_push(gt, n, false))
			fail++;
	}

	if (fail)
		xe_gt_sriov_notice(gt, "Failed to push %u of %u VF%s configurations\n",
				   fail, total_vfs - skip, str_plural(total_vfs));

	if (fail != total_vfs)
		xe_gt_sriov_dbg(gt, "pushed %u skip %u of %u VF%s configurations\n",
				total_vfs - skip - fail, skip, total_vfs, str_plural(total_vfs));
}

/**
 * xe_gt_sriov_pf_config_print_ggtt - Print GGTT configurations.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * Print GGTT configuration data for all VFs.
 * VFs without provisioned GGTT are ignored.
 *
 * This function can only be called on PF.
 */
int xe_gt_sriov_pf_config_print_ggtt(struct xe_gt *gt, struct drm_printer *p)
{
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(gt_to_xe(gt));
	const struct xe_gt_sriov_config *config;
	char buf[10];

	for (n = 1; n <= total_vfs; n++) {
		config = &gt->sriov.pf.vfs[n].config;
		if (!xe_ggtt_node_allocated(config->ggtt_region))
			continue;

		string_get_size(config->ggtt_region->base.size, 1, STRING_UNITS_2,
				buf, sizeof(buf));
		drm_printf(p, "VF%u:\t%#0llx-%#llx\t(%s)\n",
			   n, config->ggtt_region->base.start,
			   config->ggtt_region->base.start + config->ggtt_region->base.size - 1,
			   buf);
	}

	return 0;
}

/**
 * xe_gt_sriov_pf_config_print_ctxs - Print GuC context IDs configurations.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * Print GuC context ID allocations across all VFs.
 * VFs without GuC context IDs are skipped.
 *
 * This function can only be called on PF.
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_print_ctxs(struct xe_gt *gt, struct drm_printer *p)
{
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(gt_to_xe(gt));
	const struct xe_gt_sriov_config *config;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));

	for (n = 1; n <= total_vfs; n++) {
		config = &gt->sriov.pf.vfs[n].config;
		if (!config->num_ctxs)
			continue;

		drm_printf(p, "VF%u:\t%u-%u\t(%u)\n",
			   n,
			   config->begin_ctx,
			   config->begin_ctx + config->num_ctxs - 1,
			   config->num_ctxs);
	}

	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));
	return 0;
}

/**
 * xe_gt_sriov_pf_config_print_dbs - Print GuC doorbell ID configurations.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * Print GuC doorbell IDs allocations across all VFs.
 * VFs without GuC doorbell IDs are skipped.
 *
 * This function can only be called on PF.
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_print_dbs(struct xe_gt *gt, struct drm_printer *p)
{
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(gt_to_xe(gt));
	const struct xe_gt_sriov_config *config;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));

	for (n = 1; n <= total_vfs; n++) {
		config = &gt->sriov.pf.vfs[n].config;
		if (!config->num_dbs)
			continue;

		drm_printf(p, "VF%u:\t%u-%u\t(%u)\n",
			   n,
			   config->begin_db,
			   config->begin_db + config->num_dbs - 1,
			   config->num_dbs);
	}

	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));
	return 0;
}

/**
 * xe_gt_sriov_pf_config_print_lmem - Print LMEM configurations.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * Print LMEM allocations across all VFs.
 * VFs without LMEM allocation are skipped.
 *
 * This function can only be called on PF.
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_config_print_lmem(struct xe_gt *gt, struct drm_printer *p)
{
	unsigned int n, total_vfs = xe_sriov_pf_get_totalvfs(gt_to_xe(gt));
	const struct xe_gt_sriov_config *config;
	char buf[10];

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));

	for (n = 1; n <= total_vfs; n++) {
		config = &gt->sriov.pf.vfs[n].config;
		if (!config->lmem_obj)
			continue;

		string_get_size(xe_bo_size(config->lmem_obj), 1, STRING_UNITS_2,
				buf, sizeof(buf));
		drm_printf(p, "VF%u:\t%zu\t(%s)\n",
			   n, xe_bo_size(config->lmem_obj), buf);
	}

	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));
	return 0;
}

/**
 * xe_gt_sriov_pf_config_print_available_ggtt - Print available GGTT ranges.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * Print GGTT ranges that are available for the provisioning.
 *
 * This function can only be called on PF.
 */
int xe_gt_sriov_pf_config_print_available_ggtt(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_ggtt *ggtt = gt_to_tile(gt)->mem.ggtt;
	u64 alignment = pf_get_ggtt_alignment(gt);
	u64 spare, avail, total;
	char buf[10];

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));

	spare = pf_get_spare_ggtt(gt);
	total = xe_ggtt_print_holes(ggtt, alignment, p);

	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	string_get_size(total, 1, STRING_UNITS_2, buf, sizeof(buf));
	drm_printf(p, "total:\t%llu\t(%s)\n", total, buf);

	string_get_size(spare, 1, STRING_UNITS_2, buf, sizeof(buf));
	drm_printf(p, "spare:\t%llu\t(%s)\n", spare, buf);

	avail = total > spare ? total - spare : 0;

	string_get_size(avail, 1, STRING_UNITS_2, buf, sizeof(buf));
	drm_printf(p, "avail:\t%llu\t(%s)\n", avail, buf);

	return 0;
}
