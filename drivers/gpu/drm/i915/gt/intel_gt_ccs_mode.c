// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_engine_user.h"
#include "intel_gt_ccs_mode.h"
#include "intel_gt_pm.h"
#include "intel_gt_print.h"
#include "intel_gt_regs.h"
#include "intel_gt_sysfs.h"
#include "sysfs_engines.h"

static void intel_gt_apply_ccs_mode(struct intel_gt *gt)
{
	unsigned long ccs_mask = gt->ccs.id_mask;
	u32 mode_val = 0;

	/*
	 * The mode has two bit dedicated for each engine
	 * that will be used for the CCS balancing algorithm:
	 *
	 *    BIT | CCS slice
	 *   ------------------
	 *     0  | CCS slice
	 *     1  |     0
	 *   ------------------
	 *     2  | CCS slice
	 *     3  |     1
	 *   ------------------
	 *     4  | CCS slice
	 *     5  |     2
	 *   ------------------
	 *     6  | CCS slice
	 *     7  |     3
	 *   ------------------
	 *
	 * When a CCS slice is not available, then we will write 0x7,
	 * oterwise we will write the user engine id which load will
	 * be forwarded to that slice.
	 *
	 * The possible configurations are:
	 *
	 * 1 engine (ccs0):
	 *   slice 0, 1, 2, 3: ccs0
	 *
	 * 2 engines (ccs0, ccs1):
	 *   slice 0, 2: ccs0
	 *   slice 1, 3: ccs1
	 *
	 * 4 engines (ccs0, ccs1, ccs2, ccs3):
	 *   slice 0: ccs0
	 *   slice 1: ccs1
	 *   slice 2: ccs2
	 *   slice 3: ccs3
	 */

	switch (COUNT_NUM_BITS(ccs_mask)) {
		case 1:
			// CCC0 owns all EUs
			mode_val = CCS_MODE_VALUE(CCS0, CCS0, CCS0, CCS0);
			break;
		case 2:
			// 2 CCS enabled. 50% EU for each
			mode_val = CCS_MODE_VALUE(CCS0, CCS0, CCS1, CCS1);
			break;
		case 4:
			// 4 CCS enabled.  25% EU for each
			mode_val = CCS_MODE_VALUE(CCS0, CCS1, CCS2, CCS3);
			break;
	}

	gt->ccs.mode_reg_val = mode_val;
	// Set ccs mode to desired value.
	intel_uncore_write(gt->uncore, XEHP_CCS_MODE,
							gt->ccs.mode_reg_val);
}

static void __update_ccs_mask(struct intel_gt *gt, u32 ccs_mode)
{
	unsigned long cslices_mask = CCS_MASK(gt);
	int i;

	/* Mask off all the CCS engines */
	gt->ccs.id_mask = 0;

	for_each_set_bit(i, &cslices_mask, I915_MAX_CCS) {
		gt->ccs.id_mask |= BIT(i);

		ccs_mode--;
		if (!ccs_mode)
			break;
	}

	/*
	 * It's impossible for 'ccs_mode' to be zero at this point.
	 * This scenario would only occur if the 'ccs_mode' provided by
	 * the caller exceeded the total number of CCS engines, a condition
	 * we check before calling the 'update_ccs_mask()' function.
	 */
	GEM_BUG_ON(ccs_mode);

	/* Initialize the CCS mode setting */
	intel_gt_apply_ccs_mode(gt);
}

static void update_ccs_mask(struct intel_gt *gt, u32 ccs_mode)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp;

	__update_ccs_mask(gt, ccs_mode);

	/* Update workaround values */
	for_each_engine_masked(engine, gt, gt->ccs.id_mask, tmp) {
		struct i915_wa_list *wal = &engine->wa_list;
		struct i915_wa *wa;
		int i;
		for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
			if (!i915_mmio_reg_equal(wa->reg, XEHP_CCS_MODE))
				continue;

			wa->set = gt->ccs.mode_reg_val;
			wa->read = gt->ccs.mode_reg_val;
		}
	}
}

void intel_gt_ccs_mode_init(struct intel_gt *gt)
{
	if (!IS_DG2(gt->i915))
		return;

	/*
	 * Set CCS balance mode 1 in the ccs_mask.
	 *
	 * During init the workaround are not set up yet.
	 */
	__update_ccs_mask(gt, 1);
}

static int rb_engine_cmp(struct rb_node *rb_new, const struct rb_node *rb_old)
{
	struct intel_engine_cs *new = rb_to_uabi_engine(rb_new);
	struct intel_engine_cs *old = rb_to_uabi_engine(rb_old);

	if (new->uabi_class - old->uabi_class == 0)
		return new->uabi_instance - old->uabi_instance;

	return new->uabi_class - old->uabi_class;
}

static void add_uabi_ccs_engines(struct intel_gt *gt, u32 ccs_mode)
{
	struct drm_i915_private *i915 = gt->i915;
	intel_engine_mask_t new_ccs_mask, tmp;
	struct intel_engine_cs *e;

	/* Store the current ccs mask */
	new_ccs_mask = gt->ccs.id_mask;
	update_ccs_mask(gt, ccs_mode);

	/*
	 * Store only the mask of the CCS engines that need to be added by
	 * removing from the new mask the engines that are already active
	 */
	new_ccs_mask = gt->ccs.id_mask & ~new_ccs_mask;
	new_ccs_mask <<= CCS0;

	mutex_lock(&i915->uabi_engines_mutex);
	for_each_engine_masked(e, gt, new_ccs_mask, tmp) {
		int err;

		i915->engine_uabi_class_count[I915_ENGINE_CLASS_COMPUTE]++;
		/*
		 * The engine is now inserted and marked as valid.
		 *
		 * rb_find_add() should always return NULL. If it returns a
		 * pointer to an rb_node it means that it found the engine we
		 * are trying to insert which means that something is really
		 * wrong.
		 */
		rb_find_add(&e->uabi_node,
				       &i915->uabi_engines, rb_engine_cmp);

		/* We inserted the engine, let's check if now we can find it */
		if (intel_engine_lookup_user(i915, e->uabi_class,
						    e->uabi_instance) != e) {
			gt_warn(gt, "Engine %s not inserted", e->name);
		}

		/*
		 * If the engine has never been used before (e.g. we are moving
		 * for the first time from CCS mode 1 to CCS mode 2 or 4), then
		 * also its sysfs entry has never been created. In this case its
		 * value will be null and we need to allocate it.
		 */
		if (!e->kobj)
			err = intel_engine_add_single_sysfs(e);
		else
			err = kobject_add(e->kobj,
					  i915->sysfs_engine, "%s", e->name);

		if (err)
			gt_warn(gt,
				"Unable to create sysfs entries for %s engine",
				e->name);
	}
	mutex_unlock(&i915->uabi_engines_mutex);
}

static void remove_uabi_ccs_engines(struct intel_gt *gt, u8 ccs_mode)
{
	struct drm_i915_private *i915 = gt->i915;
	intel_engine_mask_t new_ccs_mask, tmp;
	struct intel_engine_cs *e;

	/* Store the current ccs mask */
	new_ccs_mask = gt->ccs.id_mask;
	update_ccs_mask(gt, ccs_mode);

	/*
	 * Store only the mask of the CCS engines that need to be removed by
	 * unmasking them from the new mask the engines that are already active
	 */
	new_ccs_mask = new_ccs_mask & ~gt->ccs.id_mask;
	new_ccs_mask <<= CCS0;

	mutex_lock(&i915->uabi_engines_mutex);
	for_each_engine_masked(e, gt, new_ccs_mask, tmp) {
		i915->engine_uabi_class_count[I915_ENGINE_CLASS_COMPUTE]--;

		rb_erase(&e->uabi_node, &i915->uabi_engines);
		RB_CLEAR_NODE(&e->uabi_node);

		/* Remove sysfs entries */
		kobject_del(e->kobj);
	}
	mutex_unlock(&i915->uabi_engines_mutex);
}

static ssize_t num_cslices_show(struct device *dev,
				struct device_attribute *attr,
				char *buff)
{
	struct intel_gt *gt = kobj_to_gt(&dev->kobj);
	u32 num_slices;

	num_slices = hweight32(CCS_MASK(gt));

	return sysfs_emit(buff, "%u\n", num_slices);
}
static DEVICE_ATTR_RO(num_cslices);

static ssize_t ccs_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buff)
{
	struct intel_gt *gt = kobj_to_gt(&dev->kobj);
	u32 ccs_mode;

	ccs_mode = hweight32(gt->ccs.id_mask);

	return sysfs_emit(buff, "%u\n", ccs_mode);
}

static ssize_t ccs_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buff, size_t count)
{
	struct intel_gt *gt = kobj_to_gt(&dev->kobj);
	int num_cslices = hweight32(CCS_MASK(gt));
	int ccs_mode = hweight32(gt->ccs.id_mask);
	ssize_t ret;
	u32 val;

	ret = kstrtou32(buff, 0, &val);
	if (ret)
		return ret;

	/*
	 * As of now possible values to be set are 1, 2, 4,
	 * up to the maximum number of available slices
	 */
	if (!val || val > num_cslices || (num_cslices % val))
		return -EINVAL;

	/* Let's wait until the GT is no longer in use */
	ret = intel_gt_pm_wait_for_idle(gt);
	if (ret)
		return ret;

	mutex_lock(&gt->wakeref.mutex);

	/*
	 * Let's check again that the GT is idle,
	 * we don't want to change the CCS mode
	 * while someone is using the GT
	 */
	if (intel_gt_pm_is_awake(gt)) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * Nothing to do if the requested setting
	 * is the same as the current one
	 */
	if (val == ccs_mode)
		goto out;
	else if (val > ccs_mode)
		add_uabi_ccs_engines(gt, val);
	else
		remove_uabi_ccs_engines(gt, val);

out:
	mutex_unlock(&gt->wakeref.mutex);

	return ret ?: count;
}
static DEVICE_ATTR_RW(ccs_mode);

void intel_gt_sysfs_ccs_init(struct intel_gt *gt)
{
	if (sysfs_create_file(&gt->sysfs_gt, &dev_attr_num_cslices.attr))
		gt_warn(gt, "Failed to create sysfs num_cslices files\n");

	/*
	 * Do not create the ccs_mode file for non DG2 platforms
	 * because they don't need it as they have only one CCS engine
	 */
	if (!IS_DG2(gt->i915))
		return;

	if (sysfs_create_file(&gt->sysfs_gt, &dev_attr_ccs_mode.attr))
		gt_warn(gt, "Failed to create sysfs ccs_mode files\n");
}
