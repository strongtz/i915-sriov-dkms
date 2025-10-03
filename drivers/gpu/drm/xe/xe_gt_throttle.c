// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include <regs/xe_gt_regs.h>
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_throttle.h"
#include "xe_mmio.h"
#include "xe_pm.h"

/**
 * DOC: Xe GT Throttle
 *
 * Provides sysfs entries and other helpers for frequency throttle reasons in GT
 *
 * device/gt#/freq0/throttle/status - Overall status
 * device/gt#/freq0/throttle/reason_pl1 - Frequency throttle due to PL1
 * device/gt#/freq0/throttle/reason_pl2 - Frequency throttle due to PL2
 * device/gt#/freq0/throttle/reason_pl4 - Frequency throttle due to PL4, Iccmax etc.
 * device/gt#/freq0/throttle/reason_thermal - Frequency throttle due to thermal
 * device/gt#/freq0/throttle/reason_prochot - Frequency throttle due to prochot
 * device/gt#/freq0/throttle/reason_ratl - Frequency throttle due to RATL
 * device/gt#/freq0/throttle/reason_vr_thermalert - Frequency throttle due to VR THERMALERT
 * device/gt#/freq0/throttle/reason_vr_tdc -  Frequency throttle due to VR TDC
 */

static struct xe_gt *
dev_to_gt(struct device *dev)
{
	return kobj_to_gt(dev->kobj.parent);
}

u32 xe_gt_throttle_get_limit_reasons(struct xe_gt *gt)
{
	u32 reg;

	xe_pm_runtime_get(gt_to_xe(gt));
	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(&gt->mmio, MTL_MEDIA_PERF_LIMIT_REASONS);
	else
		reg = xe_mmio_read32(&gt->mmio, GT0_PERF_LIMIT_REASONS);
	xe_pm_runtime_put(gt_to_xe(gt));

	return reg;
}

static u32 read_status(struct xe_gt *gt)
{
	u32 status = xe_gt_throttle_get_limit_reasons(gt) & GT0_PERF_LIMIT_REASONS_MASK;

	xe_gt_dbg(gt, "throttle reasons: 0x%08x\n", status);
	return status;
}

static u32 read_reason_pl1(struct xe_gt *gt)
{
	u32 pl1 = xe_gt_throttle_get_limit_reasons(gt) & POWER_LIMIT_1_MASK;

	return pl1;
}

static u32 read_reason_pl2(struct xe_gt *gt)
{
	u32 pl2 = xe_gt_throttle_get_limit_reasons(gt) & POWER_LIMIT_2_MASK;

	return pl2;
}

static u32 read_reason_pl4(struct xe_gt *gt)
{
	u32 pl4 = xe_gt_throttle_get_limit_reasons(gt) & POWER_LIMIT_4_MASK;

	return pl4;
}

static u32 read_reason_thermal(struct xe_gt *gt)
{
	u32 thermal = xe_gt_throttle_get_limit_reasons(gt) & THERMAL_LIMIT_MASK;

	return thermal;
}

static u32 read_reason_prochot(struct xe_gt *gt)
{
	u32 prochot = xe_gt_throttle_get_limit_reasons(gt) & PROCHOT_MASK;

	return prochot;
}

static u32 read_reason_ratl(struct xe_gt *gt)
{
	u32 ratl = xe_gt_throttle_get_limit_reasons(gt) & RATL_MASK;

	return ratl;
}

static u32 read_reason_vr_thermalert(struct xe_gt *gt)
{
	u32 thermalert = xe_gt_throttle_get_limit_reasons(gt) & VR_THERMALERT_MASK;

	return thermalert;
}

static u32 read_reason_vr_tdc(struct xe_gt *gt)
{
	u32 tdc = xe_gt_throttle_get_limit_reasons(gt) & VR_TDC_MASK;

	return tdc;
}

static ssize_t status_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool status = !!read_status(gt);

	return sysfs_emit(buff, "%u\n", status);
}
static struct kobj_attribute attr_status = __ATTR_RO(status);

static ssize_t reason_pl1_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl1 = !!read_reason_pl1(gt);

	return sysfs_emit(buff, "%u\n", pl1);
}
static struct kobj_attribute attr_reason_pl1 = __ATTR_RO(reason_pl1);

static ssize_t reason_pl2_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl2 = !!read_reason_pl2(gt);

	return sysfs_emit(buff, "%u\n", pl2);
}
static struct kobj_attribute attr_reason_pl2 = __ATTR_RO(reason_pl2);

static ssize_t reason_pl4_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool pl4 = !!read_reason_pl4(gt);

	return sysfs_emit(buff, "%u\n", pl4);
}
static struct kobj_attribute attr_reason_pl4 = __ATTR_RO(reason_pl4);

static ssize_t reason_thermal_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermal = !!read_reason_thermal(gt);

	return sysfs_emit(buff, "%u\n", thermal);
}
static struct kobj_attribute attr_reason_thermal = __ATTR_RO(reason_thermal);

static ssize_t reason_prochot_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool prochot = !!read_reason_prochot(gt);

	return sysfs_emit(buff, "%u\n", prochot);
}
static struct kobj_attribute attr_reason_prochot = __ATTR_RO(reason_prochot);

static ssize_t reason_ratl_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool ratl = !!read_reason_ratl(gt);

	return sysfs_emit(buff, "%u\n", ratl);
}
static struct kobj_attribute attr_reason_ratl = __ATTR_RO(reason_ratl);

static ssize_t reason_vr_thermalert_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool thermalert = !!read_reason_vr_thermalert(gt);

	return sysfs_emit(buff, "%u\n", thermalert);
}
static struct kobj_attribute attr_reason_vr_thermalert = __ATTR_RO(reason_vr_thermalert);

static ssize_t reason_vr_tdc_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buff)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_gt *gt = dev_to_gt(dev);
	bool tdc = !!read_reason_vr_tdc(gt);

	return sysfs_emit(buff, "%u\n", tdc);
}
static struct kobj_attribute attr_reason_vr_tdc = __ATTR_RO(reason_vr_tdc);

static struct attribute *throttle_attrs[] = {
	&attr_status.attr,
	&attr_reason_pl1.attr,
	&attr_reason_pl2.attr,
	&attr_reason_pl4.attr,
	&attr_reason_thermal.attr,
	&attr_reason_prochot.attr,
	&attr_reason_ratl.attr,
	&attr_reason_vr_thermalert.attr,
	&attr_reason_vr_tdc.attr,
	NULL
};

static const struct attribute_group throttle_group_attrs = {
	.name = "throttle",
	.attrs = throttle_attrs,
};

static void gt_throttle_sysfs_fini(void *arg)
{
	struct xe_gt *gt = arg;

	sysfs_remove_group(gt->freq, &throttle_group_attrs);
}

int xe_gt_throttle_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	err = sysfs_create_group(gt->freq, &throttle_group_attrs);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, gt_throttle_sysfs_fini, gt);
}
