// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/units.h>

#include <drm/drm_managed.h>
#include "regs/xe_gt_regs.h"
#include "regs/xe_mchbar_regs.h"
#include "regs/xe_pcode_regs.h"
#include "xe_device.h"
#include "xe_hwmon.h"
#include "xe_mmio.h"
#include "xe_pcode.h"
#include "xe_pcode_api.h"
#include "xe_sriov.h"
#include "xe_pm.h"
#include "xe_vsec.h"
#include "regs/xe_pmt.h"

enum xe_hwmon_reg {
	REG_TEMP,
	REG_PKG_RAPL_LIMIT,
	REG_PKG_POWER_SKU,
	REG_PKG_POWER_SKU_UNIT,
	REG_GT_PERF_STATUS,
	REG_PKG_ENERGY_STATUS,
	REG_FAN_SPEED,
};

enum xe_hwmon_reg_operation {
	REG_READ32,
	REG_RMW32,
	REG_READ64,
};

enum xe_hwmon_channel {
	CHANNEL_CARD,
	CHANNEL_PKG,
	CHANNEL_VRAM,
	CHANNEL_MAX,
};

enum xe_fan_channel {
	FAN_1,
	FAN_2,
	FAN_3,
	FAN_MAX,
};

/* Attribute index for powerX_xxx_interval sysfs entries */
enum sensor_attr_power {
	SENSOR_INDEX_PSYS_PL1,
	SENSOR_INDEX_PKG_PL1,
	SENSOR_INDEX_PSYS_PL2,
	SENSOR_INDEX_PKG_PL2,
};

/*
 * For platforms that support mailbox commands for power limits, REG_PKG_POWER_SKU_UNIT is
 * not supported and below are SKU units to be used.
 */
#define PWR_UNIT	0x3
#define ENERGY_UNIT	0xe
#define TIME_UNIT	0xa

/*
 * SF_* - scale factors for particular quantities according to hwmon spec.
 */
#define SF_POWER	1000000		/* microwatts */
#define SF_CURR		1000		/* milliamperes */
#define SF_VOLTAGE	1000		/* millivolts */
#define SF_ENERGY	1000000		/* microjoules */
#define SF_TIME		1000		/* milliseconds */

/*
 * PL*_HWMON_ATTR - mapping of hardware power limits to corresponding hwmon power attribute.
 */
#define PL1_HWMON_ATTR	hwmon_power_max
#define PL2_HWMON_ATTR	hwmon_power_cap

#define PWR_ATTR_TO_STR(attr)	(((attr) == hwmon_power_max) ? "PL1" : "PL2")

/*
 * Timeout for power limit write mailbox command.
 */
#define PL_WRITE_MBX_TIMEOUT_MS	(1)

/**
 * struct xe_hwmon_energy_info - to accumulate energy
 */
struct xe_hwmon_energy_info {
	/** @reg_val_prev: previous energy reg val */
	u32 reg_val_prev;
	/** @accum_energy: accumulated energy */
	long accum_energy;
};

/**
 * struct xe_hwmon_fan_info - to cache previous fan reading
 */
struct xe_hwmon_fan_info {
	/** @reg_val_prev: previous fan reg val */
	u32 reg_val_prev;
	/** @time_prev: previous timestamp */
	u64 time_prev;
};

/**
 * struct xe_hwmon - xe hwmon data structure
 */
struct xe_hwmon {
	/** @hwmon_dev: hwmon device for xe */
	struct device *hwmon_dev;
	/** @xe: Xe device */
	struct xe_device *xe;
	/** @hwmon_lock: lock for rw attributes*/
	struct mutex hwmon_lock;
	/** @scl_shift_power: pkg power unit */
	int scl_shift_power;
	/** @scl_shift_energy: pkg energy unit */
	int scl_shift_energy;
	/** @scl_shift_time: pkg time unit */
	int scl_shift_time;
	/** @ei: Energy info for energyN_input */
	struct xe_hwmon_energy_info ei[CHANNEL_MAX];
	/** @fi: Fan info for fanN_input */
	struct xe_hwmon_fan_info fi[FAN_MAX];
	/** @boot_power_limit_read: is boot power limits read */
	bool boot_power_limit_read;
	/** @pl1_on_boot: power limit PL1 on boot */
	u32 pl1_on_boot[CHANNEL_MAX];
	/** @pl2_on_boot: power limit PL2 on boot */
	u32 pl2_on_boot[CHANNEL_MAX];

};

static int xe_hwmon_pcode_read_power_limit(const struct xe_hwmon *hwmon, u32 attr, int channel,
					   u32 *uval)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(hwmon->xe);
	u32 val0 = 0, val1 = 0;
	int ret = 0;

	ret = xe_pcode_read(root_tile, PCODE_MBOX(PCODE_POWER_SETUP,
						  (channel == CHANNEL_CARD) ?
						  READ_PSYSGPU_POWER_LIMIT :
						  READ_PACKAGE_POWER_LIMIT,
						  hwmon->boot_power_limit_read ?
						  READ_PL_FROM_PCODE : READ_PL_FROM_FW),
						  &val0, &val1);

	if (ret) {
		drm_dbg(&hwmon->xe->drm, "read failed ch %d val0 0x%08x, val1 0x%08x, ret %d\n",
			channel, val0, val1, ret);
		*uval = 0;
		return ret;
	}

	/* return the value only if limit is enabled */
	if (attr == PL1_HWMON_ATTR)
		*uval = (val0 & PWR_LIM_EN) ? val0 : 0;
	else if (attr == PL2_HWMON_ATTR)
		*uval = (val1 & PWR_LIM_EN) ? val1 : 0;
	else if (attr == hwmon_power_label)
		*uval = (val0 & PWR_LIM_EN) ? 1 : (val1 & PWR_LIM_EN) ? 1 : 0;
	else
		*uval = 0;

	return ret;
}

static int xe_hwmon_pcode_rmw_power_limit(const struct xe_hwmon *hwmon, u32 attr, u8 channel,
					  u32 clr, u32 set)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(hwmon->xe);
	u32 val0, val1;
	int ret = 0;

	ret = xe_pcode_read(root_tile, PCODE_MBOX(PCODE_POWER_SETUP,
						  (channel == CHANNEL_CARD) ?
						  READ_PSYSGPU_POWER_LIMIT :
						  READ_PACKAGE_POWER_LIMIT,
						  hwmon->boot_power_limit_read ?
						  READ_PL_FROM_PCODE : READ_PL_FROM_FW),
						  &val0, &val1);

	if (ret)
		drm_dbg(&hwmon->xe->drm, "read failed ch %d val0 0x%08x, val1 0x%08x, ret %d\n",
			channel, val0, val1, ret);

	if (attr == PL1_HWMON_ATTR)
		val0 = (val0 & ~clr) | set;
	else if (attr == PL2_HWMON_ATTR)
		val1 = (val1 & ~clr) | set;
	else
		return -EIO;

	ret = xe_pcode_write64_timeout(root_tile, PCODE_MBOX(PCODE_POWER_SETUP,
							     (channel == CHANNEL_CARD) ?
							     WRITE_PSYSGPU_POWER_LIMIT :
							     WRITE_PACKAGE_POWER_LIMIT, 0),
							     val0, val1, PL_WRITE_MBX_TIMEOUT_MS);
	if (ret)
		drm_dbg(&hwmon->xe->drm, "write failed ch %d val0 0x%08x, val1 0x%08x, ret %d\n",
			channel, val0, val1, ret);
	return ret;
}

static struct xe_reg xe_hwmon_get_reg(struct xe_hwmon *hwmon, enum xe_hwmon_reg hwmon_reg,
				      int channel)
{
	struct xe_device *xe = hwmon->xe;

	switch (hwmon_reg) {
	case REG_TEMP:
		if (xe->info.platform == XE_BATTLEMAGE) {
			if (channel == CHANNEL_PKG)
				return BMG_PACKAGE_TEMPERATURE;
			else if (channel == CHANNEL_VRAM)
				return BMG_VRAM_TEMPERATURE;
		} else if (xe->info.platform == XE_DG2) {
			if (channel == CHANNEL_PKG)
				return PCU_CR_PACKAGE_TEMPERATURE;
			else if (channel == CHANNEL_VRAM)
				return BMG_VRAM_TEMPERATURE;
		}
		break;
	case REG_PKG_RAPL_LIMIT:
		if (xe->info.platform == XE_PVC && channel == CHANNEL_PKG)
			return PVC_GT0_PACKAGE_RAPL_LIMIT;
		else if ((xe->info.platform == XE_DG2) && (channel == CHANNEL_PKG))
			return PCU_CR_PACKAGE_RAPL_LIMIT;
		break;
	case REG_PKG_POWER_SKU:
		if (xe->info.platform == XE_PVC && channel == CHANNEL_PKG)
			return PVC_GT0_PACKAGE_POWER_SKU;
		else if ((xe->info.platform == XE_DG2) && (channel == CHANNEL_PKG))
			return PCU_CR_PACKAGE_POWER_SKU;
		break;
	case REG_PKG_POWER_SKU_UNIT:
		if (xe->info.platform == XE_PVC)
			return PVC_GT0_PACKAGE_POWER_SKU_UNIT;
		else if (xe->info.platform == XE_DG2)
			return PCU_CR_PACKAGE_POWER_SKU_UNIT;
		break;
	case REG_GT_PERF_STATUS:
		if (xe->info.platform == XE_DG2 && channel == CHANNEL_PKG)
			return GT_PERF_STATUS;
		break;
	case REG_PKG_ENERGY_STATUS:
		if (xe->info.platform == XE_PVC && channel == CHANNEL_PKG) {
			return PVC_GT0_PLATFORM_ENERGY_STATUS;
		} else if ((xe->info.platform == XE_DG2) && (channel == CHANNEL_PKG)) {
			return PCU_CR_PACKAGE_ENERGY_STATUS;
		}
		break;
	case REG_FAN_SPEED:
		if (channel == FAN_1)
			return BMG_FAN_1_SPEED;
		else if (channel == FAN_2)
			return BMG_FAN_2_SPEED;
		else if (channel == FAN_3)
			return BMG_FAN_3_SPEED;
		break;
	default:
		drm_warn(&xe->drm, "Unknown xe hwmon reg id: %d\n", hwmon_reg);
		break;
	}

	return XE_REG(0);
}

#define PL_DISABLE 0

/*
 * HW allows arbitrary PL1 limits to be set but silently clamps these values to
 * "typical but not guaranteed" min/max values in REG_PKG_POWER_SKU. Follow the
 * same pattern for sysfs, allow arbitrary PL1 limits to be set but display
 * clamped values when read.
 */
static void xe_hwmon_power_max_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *value)
{
	u32 reg_val = 0;
	struct xe_device *xe = hwmon->xe;
	struct xe_reg rapl_limit, pkg_power_sku;
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);

	mutex_lock(&hwmon->hwmon_lock);

	if (hwmon->xe->info.has_mbx_power_limits) {
		xe_hwmon_pcode_read_power_limit(hwmon, attr, channel, &reg_val);
	} else {
		rapl_limit = xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT, channel);
		pkg_power_sku = xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU, channel);
		reg_val = xe_mmio_read32(mmio, rapl_limit);
	}

	/* Check if PL limits are disabled. */
	if (!(reg_val & PWR_LIM_EN)) {
		*value = PL_DISABLE;
		drm_info(&hwmon->xe->drm, "%s disabled for channel %d, val 0x%08x\n",
			 PWR_ATTR_TO_STR(attr), channel, reg_val);
		goto unlock;
	}

	reg_val = REG_FIELD_GET(PWR_LIM_VAL, reg_val);
	*value = mul_u32_u32(reg_val, SF_POWER) >> hwmon->scl_shift_power;

	/* For platforms with mailbox power limit support clamping would be done by pcode. */
	if (!hwmon->xe->info.has_mbx_power_limits) {
		u64 pkg_pwr, min, max;

		pkg_pwr = xe_mmio_read64_2x32(mmio, pkg_power_sku);
		min = REG_FIELD_GET(PKG_MIN_PWR, pkg_pwr);
		max = REG_FIELD_GET(PKG_MAX_PWR, pkg_pwr);
		min = mul_u64_u32_shr(min, SF_POWER, hwmon->scl_shift_power);
		max = mul_u64_u32_shr(max, SF_POWER, hwmon->scl_shift_power);
		if (min && max)
			*value = clamp_t(u64, *value, min, max);
	}
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
}

static int xe_hwmon_power_max_write(struct xe_hwmon *hwmon, u32 attr, int channel, long value)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	int ret = 0;
	u32 reg_val, max;
	struct xe_reg rapl_limit;
	u64 max_supp_power_limit = 0;

	mutex_lock(&hwmon->hwmon_lock);

	rapl_limit = xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT, channel);

	/* Disable Power Limit and verify, as limit cannot be disabled on all platforms. */
	if (value == PL_DISABLE) {
		if (hwmon->xe->info.has_mbx_power_limits) {
			drm_dbg(&hwmon->xe->drm, "disabling %s on channel %d\n",
				PWR_ATTR_TO_STR(attr), channel);
			xe_hwmon_pcode_rmw_power_limit(hwmon, attr, channel, PWR_LIM_EN, 0);
			xe_hwmon_pcode_read_power_limit(hwmon, attr, channel, &reg_val);
		} else {
			reg_val = xe_mmio_rmw32(mmio, rapl_limit, PWR_LIM_EN, 0);
			reg_val = xe_mmio_read32(mmio, rapl_limit);
		}

		if (reg_val & PWR_LIM_EN) {
			drm_warn(&hwmon->xe->drm, "Power limit disable is not supported!\n");
			ret = -EOPNOTSUPP;
		}
		goto unlock;
	}

	/*
	 * If the sysfs value exceeds the maximum pcode supported power limit value, clamp it to
	 * the supported maximum (U12.3 format).
	 * This is to avoid truncation during reg_val calculation below and ensure the valid
	 * power limit is sent for pcode which would clamp it to card-supported value.
	 */
	max_supp_power_limit = ((PWR_LIM_VAL) >> hwmon->scl_shift_power) * SF_POWER;
	if (value > max_supp_power_limit) {
		value = max_supp_power_limit;
		drm_info(&hwmon->xe->drm,
			 "Power limit clamped as selected %s exceeds channel %d limit\n",
			 PWR_ATTR_TO_STR(attr), channel);
	}

	/* Computation in 64-bits to avoid overflow. Round to nearest. */
	reg_val = DIV_ROUND_CLOSEST_ULL((u64)value << hwmon->scl_shift_power, SF_POWER);

	/*
	 * Clamp power limit to GPU firmware default as maximum, as an additional protection to
	 * pcode clamp.
	 */
	if (hwmon->xe->info.has_mbx_power_limits) {
		max = (attr == PL1_HWMON_ATTR) ?
		       hwmon->pl1_on_boot[channel] : hwmon->pl2_on_boot[channel];
		max = REG_FIELD_PREP(PWR_LIM_VAL, max);
		if (reg_val > max) {
			reg_val = max;
			drm_dbg(&hwmon->xe->drm,
				"Clamping power limit to GPU firmware default 0x%x\n",
				reg_val);
		}
	}

	reg_val = PWR_LIM_EN | REG_FIELD_PREP(PWR_LIM_VAL, reg_val);

	if (hwmon->xe->info.has_mbx_power_limits)
		ret = xe_hwmon_pcode_rmw_power_limit(hwmon, attr, channel, PWR_LIM, reg_val);
	else
		reg_val = xe_mmio_rmw32(mmio, rapl_limit, PWR_LIM, reg_val);
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static void xe_hwmon_power_rated_max_read(struct xe_hwmon *hwmon, u32 attr, int channel,
					  long *value)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	u32 reg_val;

	if (hwmon->xe->info.has_mbx_power_limits) {
		/* PL1 is rated max if supported. */
		xe_hwmon_pcode_read_power_limit(hwmon, PL1_HWMON_ATTR, channel, &reg_val);
	} else {
		/*
		 * This sysfs file won't be visible if REG_PKG_POWER_SKU is invalid, so valid check
		 * for this register can be skipped.
		 * See xe_hwmon_power_is_visible.
		 */
		struct xe_reg reg = xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU, channel);

		reg_val = xe_mmio_read32(mmio, reg);
	}

	reg_val = REG_FIELD_GET(PKG_TDP, reg_val);
	*value = mul_u64_u32_shr(reg_val, SF_POWER, hwmon->scl_shift_power);
}

/*
 * xe_hwmon_energy_get - Obtain energy value
 *
 * The underlying energy hardware register is 32-bits and is subject to
 * overflow. How long before overflow? For example, with an example
 * scaling bit shift of 14 bits (see register *PACKAGE_POWER_SKU_UNIT) and
 * a power draw of 1000 watts, the 32-bit counter will overflow in
 * approximately 4.36 minutes.
 *
 * Examples:
 *    1 watt:  (2^32 >> 14) /    1 W / (60 * 60 * 24) secs/day -> 3 days
 * 1000 watts: (2^32 >> 14) / 1000 W / 60             secs/min -> 4.36 minutes
 *
 * The function significantly increases overflow duration (from 4.36
 * minutes) by accumulating the energy register into a 'long' as allowed by
 * the hwmon API. Using x86_64 128 bit arithmetic (see mul_u64_u32_shr()),
 * a 'long' of 63 bits, SF_ENERGY of 1e6 (~20 bits) and
 * hwmon->scl_shift_energy of 14 bits we have 57 (63 - 20 + 14) bits before
 * energyN_input overflows. This at 1000 W is an overflow duration of 278 years.
 */
static void
xe_hwmon_energy_get(struct xe_hwmon *hwmon, int channel, long *energy)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	struct xe_hwmon_energy_info *ei = &hwmon->ei[channel];
	u32 reg_val;
	int ret = 0;

	/* Energy is supported only for card and pkg */
	if (channel > CHANNEL_PKG) {
		*energy = 0;
		return;
	}

	if (hwmon->xe->info.platform == XE_BATTLEMAGE) {
		u64 pmt_val;

		ret = xe_pmt_telem_read(to_pci_dev(hwmon->xe->drm.dev),
					xe_mmio_read32(mmio, PUNIT_TELEMETRY_GUID),
					&pmt_val, BMG_ENERGY_STATUS_PMT_OFFSET,	sizeof(pmt_val));
		if (ret != sizeof(pmt_val)) {
			drm_warn(&hwmon->xe->drm, "energy read from pmt failed, ret %d\n", ret);
			*energy = 0;
			return;
		}

		if (channel == CHANNEL_PKG)
			reg_val = REG_FIELD_GET64(ENERGY_PKG, pmt_val);
		else
			reg_val = REG_FIELD_GET64(ENERGY_CARD, pmt_val);
	} else {
		reg_val = xe_mmio_read32(mmio, xe_hwmon_get_reg(hwmon, REG_PKG_ENERGY_STATUS,
								channel));
	}

	ei->accum_energy += reg_val - ei->reg_val_prev;
	ei->reg_val_prev = reg_val;

	*energy = mul_u64_u32_shr(ei->accum_energy, SF_ENERGY,
				  hwmon->scl_shift_energy);
}

static ssize_t
xe_hwmon_power_max_interval_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	u32 reg_val, x, y, x_w = 2; /* 2 bits */
	u64 tau4, out;
	int channel = (to_sensor_dev_attr(attr)->index % 2) ? CHANNEL_PKG : CHANNEL_CARD;
	u32 power_attr = (to_sensor_dev_attr(attr)->index > 1) ? PL2_HWMON_ATTR : PL1_HWMON_ATTR;

	int ret = 0;

	xe_pm_runtime_get(hwmon->xe);

	mutex_lock(&hwmon->hwmon_lock);

	if (hwmon->xe->info.has_mbx_power_limits) {
		ret = xe_hwmon_pcode_read_power_limit(hwmon, power_attr, channel, &reg_val);
		if (ret) {
			drm_err(&hwmon->xe->drm,
				"power interval read fail, ch %d, attr %d, val 0x%08x, ret %d\n",
				channel, power_attr, reg_val, ret);
			reg_val = 0;
		}
	} else {
		reg_val = xe_mmio_read32(mmio, xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT,
								channel));
	}

	mutex_unlock(&hwmon->hwmon_lock);

	xe_pm_runtime_put(hwmon->xe);

	x = REG_FIELD_GET(PWR_LIM_TIME_X, reg_val);
	y = REG_FIELD_GET(PWR_LIM_TIME_Y, reg_val);

	/*
	 * tau = (1 + (x / 4)) * power(2,y), x = bits(23:22), y = bits(21:17)
	 *     = (4 | x) << (y - 2)
	 *
	 * Here (y - 2) ensures a 1.x fixed point representation of 1.x
	 * As x is 2 bits so 1.x can be 1.0, 1.25, 1.50, 1.75
	 *
	 * As y can be < 2, we compute tau4 = (4 | x) << y
	 * and then add 2 when doing the final right shift to account for units
	 */
	tau4 = (u64)((1 << x_w) | x) << y;

	/* val in hwmon interface units (millisec) */
	out = mul_u64_u32_shr(tau4, SF_TIME, hwmon->scl_shift_time + x_w);

	return sysfs_emit(buf, "%llu\n", out);
}

static ssize_t
xe_hwmon_power_max_interval_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	u32 x, y, rxy, x_w = 2; /* 2 bits */
	u64 tau4, r, max_win;
	unsigned long val;
	int channel = (to_sensor_dev_attr(attr)->index % 2) ? CHANNEL_PKG : CHANNEL_CARD;
	u32 power_attr = (to_sensor_dev_attr(attr)->index > 1) ? PL2_HWMON_ATTR : PL1_HWMON_ATTR;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	/*
	 * Max HW supported tau in '(1 + (x / 4)) * power(2,y)' format, x = 0, y = 0x12.
	 * The hwmon->scl_shift_time default of 0xa results in a max tau of 256 seconds.
	 *
	 * The ideal scenario is for PKG_MAX_WIN to be read from the PKG_PWR_SKU register.
	 * However, it is observed that existing discrete GPUs does not provide correct
	 * PKG_MAX_WIN value, therefore a using default constant value. For future discrete GPUs
	 * this may get resolved, in which case PKG_MAX_WIN should be obtained from PKG_PWR_SKU.
	 */
#define PKG_MAX_WIN_DEFAULT 0x12ull

	/*
	 * val must be < max in hwmon interface units. The steps below are
	 * explained in xe_hwmon_power_max_interval_show()
	 */
	r = FIELD_PREP(PKG_MAX_WIN, PKG_MAX_WIN_DEFAULT);
	x = REG_FIELD_GET(PKG_MAX_WIN_X, r);
	y = REG_FIELD_GET(PKG_MAX_WIN_Y, r);
	tau4 = (u64)((1 << x_w) | x) << y;
	max_win = mul_u64_u32_shr(tau4, SF_TIME, hwmon->scl_shift_time + x_w);

	if (val > max_win)
		return -EINVAL;

	/* val in hw units */
	val = DIV_ROUND_CLOSEST_ULL((u64)val << hwmon->scl_shift_time, SF_TIME) + 1;

	/*
	 * Convert val to 1.x * power(2,y)
	 * y = ilog2(val)
	 * x = (val - (1 << y)) >> (y - 2)
	 */
	if (!val) {
		y = 0;
		x = 0;
	} else {
		y = ilog2(val);
		x = (val - (1ul << y)) << x_w >> y;
	}

	rxy = REG_FIELD_PREP(PWR_LIM_TIME_X, x) |
			       REG_FIELD_PREP(PWR_LIM_TIME_Y, y);

	xe_pm_runtime_get(hwmon->xe);

	mutex_lock(&hwmon->hwmon_lock);

	if (hwmon->xe->info.has_mbx_power_limits)
		xe_hwmon_pcode_rmw_power_limit(hwmon, power_attr, channel, PWR_LIM_TIME, rxy);
	else
		r = xe_mmio_rmw32(mmio, xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT, channel),
				  PWR_LIM_TIME, rxy);

	mutex_unlock(&hwmon->hwmon_lock);

	xe_pm_runtime_put(hwmon->xe);

	return count;
}

/* PSYS PL1 */
static SENSOR_DEVICE_ATTR(power1_max_interval, 0664,
			  xe_hwmon_power_max_interval_show,
			  xe_hwmon_power_max_interval_store, SENSOR_INDEX_PSYS_PL1);
/* PKG PL1 */
static SENSOR_DEVICE_ATTR(power2_max_interval, 0664,
			  xe_hwmon_power_max_interval_show,
			  xe_hwmon_power_max_interval_store, SENSOR_INDEX_PKG_PL1);
/* PSYS PL2 */
static SENSOR_DEVICE_ATTR(power1_cap_interval, 0664,
			  xe_hwmon_power_max_interval_show,
			  xe_hwmon_power_max_interval_store, SENSOR_INDEX_PSYS_PL2);
/* PKG PL2 */
static SENSOR_DEVICE_ATTR(power2_cap_interval, 0664,
			  xe_hwmon_power_max_interval_show,
			  xe_hwmon_power_max_interval_store, SENSOR_INDEX_PKG_PL2);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_power1_max_interval.dev_attr.attr,
	&sensor_dev_attr_power2_max_interval.dev_attr.attr,
	&sensor_dev_attr_power1_cap_interval.dev_attr.attr,
	&sensor_dev_attr_power2_cap_interval.dev_attr.attr,
	NULL
};

static umode_t xe_hwmon_attributes_visible(struct kobject *kobj,
					   struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	int ret = 0;
	int channel = (index % 2) ? CHANNEL_PKG : CHANNEL_CARD;
	u32 power_attr = (index > 1) ? PL2_HWMON_ATTR : PL1_HWMON_ATTR;
	u32 uval = 0;
	struct xe_reg rapl_limit;
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);

	xe_pm_runtime_get(hwmon->xe);

	if (hwmon->xe->info.has_mbx_power_limits) {
		xe_hwmon_pcode_read_power_limit(hwmon, power_attr, channel, &uval);
	} else if (power_attr != PL2_HWMON_ATTR) {
		rapl_limit = xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT, channel);
		if (xe_reg_is_valid(rapl_limit))
			uval = xe_mmio_read32(mmio, rapl_limit);
	}
	ret = (uval & PWR_LIM_EN) ? attr->mode : 0;

	xe_pm_runtime_put(hwmon->xe);

	return ret;
}

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
	.is_visible = xe_hwmon_attributes_visible,
};

static const struct attribute_group *hwmon_groups[] = {
	&hwmon_attrgroup,
	NULL
};

static const struct hwmon_channel_info * const hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(power, HWMON_P_MAX | HWMON_P_RATED_MAX | HWMON_P_LABEL | HWMON_P_CRIT |
			   HWMON_P_CAP,
			   HWMON_P_MAX | HWMON_P_RATED_MAX | HWMON_P_LABEL | HWMON_P_CAP),
	HWMON_CHANNEL_INFO(curr, HWMON_C_LABEL, HWMON_C_CRIT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(energy, HWMON_E_INPUT | HWMON_E_LABEL, HWMON_E_INPUT | HWMON_E_LABEL),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT, HWMON_F_INPUT, HWMON_F_INPUT),
	NULL
};

/* I1 is exposed as power_crit or as curr_crit depending on bit 31 */
static int xe_hwmon_pcode_read_i1(const struct xe_hwmon *hwmon, u32 *uval)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(hwmon->xe);

	/* Avoid Illegal Subcommand error */
	if (hwmon->xe->info.platform == XE_DG2)
		return -ENXIO;

	return xe_pcode_read(root_tile, PCODE_MBOX(PCODE_POWER_SETUP,
			     POWER_SETUP_SUBCOMMAND_READ_I1, 0),
			     uval, NULL);
}

static int xe_hwmon_pcode_write_i1(const struct xe_hwmon *hwmon, u32 uval)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(hwmon->xe);

	return xe_pcode_write(root_tile, PCODE_MBOX(PCODE_POWER_SETUP,
			      POWER_SETUP_SUBCOMMAND_WRITE_I1, 0),
			      (uval & POWER_SETUP_I1_DATA_MASK));
}

static int xe_hwmon_pcode_read_fan_control(const struct xe_hwmon *hwmon, u32 subcmd, u32 *uval)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(hwmon->xe);

	/* Platforms that don't return correct value */
	if (hwmon->xe->info.platform == XE_DG2 && subcmd == FSC_READ_NUM_FANS) {
		*uval = 2;
		return 0;
	}

	return xe_pcode_read(root_tile, PCODE_MBOX(FAN_SPEED_CONTROL, subcmd, 0), uval, NULL);
}

static int xe_hwmon_power_curr_crit_read(struct xe_hwmon *hwmon, int channel,
					 long *value, u32 scale_factor)
{
	int ret;
	u32 uval;

	mutex_lock(&hwmon->hwmon_lock);

	ret = xe_hwmon_pcode_read_i1(hwmon, &uval);
	if (ret)
		goto unlock;

	*value = mul_u64_u32_shr(REG_FIELD_GET(POWER_SETUP_I1_DATA_MASK, uval),
				 scale_factor, POWER_SETUP_I1_SHIFT);
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static int xe_hwmon_power_curr_crit_write(struct xe_hwmon *hwmon, int channel,
					  long value, u32 scale_factor)
{
	int ret;
	u32 uval;
	u64 max_crit_power_curr = 0;

	mutex_lock(&hwmon->hwmon_lock);

	/*
	 * If the sysfs value exceeds the pcode mailbox cmd POWER_SETUP_SUBCOMMAND_WRITE_I1
	 * max supported value, clamp it to the command's max (U10.6 format).
	 * This is to avoid truncation during uval calculation below and ensure the valid power
	 * limit is sent for pcode which would clamp it to card-supported value.
	 */
	max_crit_power_curr = (POWER_SETUP_I1_DATA_MASK >> POWER_SETUP_I1_SHIFT) * scale_factor;
	if (value > max_crit_power_curr) {
		value = max_crit_power_curr;
		drm_info(&hwmon->xe->drm,
			 "Power limit clamped as selected exceeds channel %d limit\n",
			 channel);
	}
	uval = DIV_ROUND_CLOSEST_ULL(value << POWER_SETUP_I1_SHIFT, scale_factor);
	ret = xe_hwmon_pcode_write_i1(hwmon, uval);

	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static void xe_hwmon_get_voltage(struct xe_hwmon *hwmon, int channel, long *value)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	u64 reg_val;

	reg_val = xe_mmio_read32(mmio, xe_hwmon_get_reg(hwmon, REG_GT_PERF_STATUS, channel));
	/* HW register value in units of 2.5 millivolt */
	*value = DIV_ROUND_CLOSEST(REG_FIELD_GET(VOLTAGE_MASK, reg_val) * 2500, SF_VOLTAGE);
}

static umode_t
xe_hwmon_temp_is_visible(struct xe_hwmon *hwmon, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_label:
		return xe_reg_is_valid(xe_hwmon_get_reg(hwmon, REG_TEMP, channel)) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
xe_hwmon_temp_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *val)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	u64 reg_val;

	switch (attr) {
	case hwmon_temp_input:
		reg_val = xe_mmio_read32(mmio, xe_hwmon_get_reg(hwmon, REG_TEMP, channel));

		/* HW register value is in degrees Celsius, convert to millidegrees. */
		*val = REG_FIELD_GET(TEMP_MASK, reg_val) * MILLIDEGREE_PER_DEGREE;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_power_is_visible(struct xe_hwmon *hwmon, u32 attr, int channel)
{
	u32 uval = 0;
	struct xe_reg reg;
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);

	switch (attr) {
	case hwmon_power_max:
	case hwmon_power_cap:
		if (hwmon->xe->info.has_mbx_power_limits) {
			xe_hwmon_pcode_read_power_limit(hwmon, attr, channel, &uval);
		} else if (attr != PL2_HWMON_ATTR) {
			reg = xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT, channel);
			if (xe_reg_is_valid(reg))
				uval = xe_mmio_read32(mmio, reg);
		}
		if (uval & PWR_LIM_EN) {
			drm_info(&hwmon->xe->drm, "%s is supported on channel %d\n",
				 PWR_ATTR_TO_STR(attr), channel);
			return 0664;
		}
		drm_dbg(&hwmon->xe->drm, "%s is unsupported on channel %d\n",
			PWR_ATTR_TO_STR(attr), channel);
		return 0;
	case hwmon_power_rated_max:
		if (hwmon->xe->info.has_mbx_power_limits) {
			return 0;
		} else {
			reg = xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU, channel);
			if (xe_reg_is_valid(reg))
				uval = xe_mmio_read32(mmio, reg);
			return uval ? 0444 : 0;
		}
	case hwmon_power_crit:
		if (channel == CHANNEL_CARD) {
			xe_hwmon_pcode_read_i1(hwmon, &uval);
			return (uval & POWER_SETUP_I1_WATTS) ? 0644 : 0;
		}
		break;
	case hwmon_power_label:
		if (hwmon->xe->info.has_mbx_power_limits) {
			xe_hwmon_pcode_read_power_limit(hwmon, attr, channel, &uval);
		} else {
			reg = xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU, channel);
			if (xe_reg_is_valid(reg))
				uval = xe_mmio_read32(mmio, reg);

			if (!uval) {
				reg = xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT, channel);
				if (xe_reg_is_valid(reg))
					uval = xe_mmio_read32(mmio, reg);
			}
		}
		if ((!(uval & PWR_LIM_EN)) && channel == CHANNEL_CARD) {
			xe_hwmon_pcode_read_i1(hwmon, &uval);
			return (uval & POWER_SETUP_I1_WATTS) ? 0444 : 0;
		}
		return (uval) ? 0444 : 0;
	default:
		return 0;
	}
	return 0;
}

static int
xe_hwmon_power_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_power_max:
	case hwmon_power_cap:
		xe_hwmon_power_max_read(hwmon, attr, channel, val);
		return 0;
	case hwmon_power_rated_max:
		xe_hwmon_power_rated_max_read(hwmon, attr, channel, val);
		return 0;
	case hwmon_power_crit:
		return xe_hwmon_power_curr_crit_read(hwmon, channel, val, SF_POWER);
	default:
		return -EOPNOTSUPP;
	}
}

static int
xe_hwmon_power_write(struct xe_hwmon *hwmon, u32 attr, int channel, long val)
{
	switch (attr) {
	case hwmon_power_cap:
	case hwmon_power_max:
		return xe_hwmon_power_max_write(hwmon, attr, channel, val);
	case hwmon_power_crit:
		return xe_hwmon_power_curr_crit_write(hwmon, channel, val, SF_POWER);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_curr_is_visible(const struct xe_hwmon *hwmon, u32 attr, int channel)
{
	u32 uval;

	/* hwmon sysfs attribute of current available only for package */
	if (channel != CHANNEL_PKG)
		return 0;

	switch (attr) {
	case hwmon_curr_crit:
			return (xe_hwmon_pcode_read_i1(hwmon, &uval) ||
				(uval & POWER_SETUP_I1_WATTS)) ? 0 : 0644;
	case hwmon_curr_label:
			return (xe_hwmon_pcode_read_i1(hwmon, &uval) ||
				(uval & POWER_SETUP_I1_WATTS)) ? 0 : 0444;
		break;
	default:
		return 0;
	}
	return 0;
}

static int
xe_hwmon_curr_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_curr_crit:
		return xe_hwmon_power_curr_crit_read(hwmon, channel, val, SF_CURR);
	default:
		return -EOPNOTSUPP;
	}
}

static int
xe_hwmon_curr_write(struct xe_hwmon *hwmon, u32 attr, int channel, long val)
{
	switch (attr) {
	case hwmon_curr_crit:
		return xe_hwmon_power_curr_crit_write(hwmon, channel, val, SF_CURR);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_in_is_visible(struct xe_hwmon *hwmon, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_in_input:
	case hwmon_in_label:
		return xe_reg_is_valid(xe_hwmon_get_reg(hwmon, REG_GT_PERF_STATUS,
				       channel)) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
xe_hwmon_in_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_in_input:
		xe_hwmon_get_voltage(hwmon, channel, val);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_energy_is_visible(struct xe_hwmon *hwmon, u32 attr, int channel)
{
	long energy = 0;

	switch (attr) {
	case hwmon_energy_input:
	case hwmon_energy_label:
		if (hwmon->xe->info.platform == XE_BATTLEMAGE) {
			xe_hwmon_energy_get(hwmon, channel, &energy);
			return energy ? 0444 : 0;
		} else {
			return xe_reg_is_valid(xe_hwmon_get_reg(hwmon, REG_PKG_ENERGY_STATUS,
					       channel)) ? 0444 : 0;
		}
	default:
		return 0;
	}
}

static int
xe_hwmon_energy_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_energy_input:
		xe_hwmon_energy_get(hwmon, channel, val);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_fan_is_visible(struct xe_hwmon *hwmon, u32 attr, int channel)
{
	u32 uval;

	if (!hwmon->xe->info.has_fan_control)
		return 0;

	switch (attr) {
	case hwmon_fan_input:
		if (xe_hwmon_pcode_read_fan_control(hwmon, FSC_READ_NUM_FANS, &uval))
			return 0;

		return channel < uval ? 0444 : 0;
	default:
		return 0;
	}
}

static int
xe_hwmon_fan_input_read(struct xe_hwmon *hwmon, int channel, long *val)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	struct xe_hwmon_fan_info *fi = &hwmon->fi[channel];
	u64 rotations, time_now, time;
	u32 reg_val;
	int ret = 0;

	mutex_lock(&hwmon->hwmon_lock);

	reg_val = xe_mmio_read32(mmio, xe_hwmon_get_reg(hwmon, REG_FAN_SPEED, channel));
	time_now = get_jiffies_64();

	/*
	 * HW register value is accumulated count of pulses from PWM fan with the scale
	 * of 2 pulses per rotation.
	 */
	rotations = (reg_val - fi->reg_val_prev) / 2;

	time = jiffies_delta_to_msecs(time_now - fi->time_prev);
	if (unlikely(!time)) {
		ret = -EAGAIN;
		goto unlock;
	}

	/*
	 * Calculate fan speed in RPM by time averaging two subsequent readings in minutes.
	 * RPM = number of rotations * msecs per minute / time in msecs
	 */
	*val = DIV_ROUND_UP_ULL(rotations * (MSEC_PER_SEC * 60), time);

	fi->reg_val_prev = reg_val;
	fi->time_prev = time_now;
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static int
xe_hwmon_fan_read(struct xe_hwmon *hwmon, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_fan_input:
		return xe_hwmon_fan_input_read(hwmon, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
		    u32 attr, int channel)
{
	struct xe_hwmon *hwmon = (struct xe_hwmon *)drvdata;
	int ret;

	xe_pm_runtime_get(hwmon->xe);

	switch (type) {
	case hwmon_temp:
		ret = xe_hwmon_temp_is_visible(hwmon, attr, channel);
		break;
	case hwmon_power:
		ret = xe_hwmon_power_is_visible(hwmon, attr, channel);
		break;
	case hwmon_curr:
		ret = xe_hwmon_curr_is_visible(hwmon, attr, channel);
		break;
	case hwmon_in:
		ret = xe_hwmon_in_is_visible(hwmon, attr, channel);
		break;
	case hwmon_energy:
		ret = xe_hwmon_energy_is_visible(hwmon, attr, channel);
		break;
	case hwmon_fan:
		ret = xe_hwmon_fan_is_visible(hwmon, attr, channel);
		break;
	default:
		ret = 0;
		break;
	}

	xe_pm_runtime_put(hwmon->xe);

	return ret;
}

static int
xe_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	      int channel, long *val)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	xe_pm_runtime_get(hwmon->xe);

	switch (type) {
	case hwmon_temp:
		ret = xe_hwmon_temp_read(hwmon, attr, channel, val);
		break;
	case hwmon_power:
		ret = xe_hwmon_power_read(hwmon, attr, channel, val);
		break;
	case hwmon_curr:
		ret = xe_hwmon_curr_read(hwmon, attr, channel, val);
		break;
	case hwmon_in:
		ret = xe_hwmon_in_read(hwmon, attr, channel, val);
		break;
	case hwmon_energy:
		ret = xe_hwmon_energy_read(hwmon, attr, channel, val);
		break;
	case hwmon_fan:
		ret = xe_hwmon_fan_read(hwmon, attr, channel, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	xe_pm_runtime_put(hwmon->xe);

	return ret;
}

static int
xe_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	       int channel, long val)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	xe_pm_runtime_get(hwmon->xe);

	switch (type) {
	case hwmon_power:
		ret = xe_hwmon_power_write(hwmon, attr, channel, val);
		break;
	case hwmon_curr:
		ret = xe_hwmon_curr_write(hwmon, attr, channel, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	xe_pm_runtime_put(hwmon->xe);

	return ret;
}

static int xe_hwmon_read_label(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		if (channel == CHANNEL_PKG)
			*str = "pkg";
		else if (channel == CHANNEL_VRAM)
			*str = "vram";
		return 0;
	case hwmon_power:
	case hwmon_energy:
	case hwmon_curr:
	case hwmon_in:
		if (channel == CHANNEL_CARD)
			*str = "card";
		else if (channel == CHANNEL_PKG)
			*str = "pkg";
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops hwmon_ops = {
	.is_visible = xe_hwmon_is_visible,
	.read = xe_hwmon_read,
	.write = xe_hwmon_write,
	.read_string = xe_hwmon_read_label,
};

static const struct hwmon_chip_info hwmon_chip_info = {
	.ops = &hwmon_ops,
	.info = hwmon_info,
};

static void
xe_hwmon_get_preregistration_info(struct xe_hwmon *hwmon)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(hwmon->xe);
	long energy, fan_speed;
	u64 val_sku_unit = 0;
	int channel;
	struct xe_reg pkg_power_sku_unit;

	if (hwmon->xe->info.has_mbx_power_limits) {
		/* Check if GPU firmware support mailbox power limits commands. */
		if (xe_hwmon_pcode_read_power_limit(hwmon, PL1_HWMON_ATTR, CHANNEL_CARD,
						    &hwmon->pl1_on_boot[CHANNEL_CARD]) |
		    xe_hwmon_pcode_read_power_limit(hwmon, PL1_HWMON_ATTR, CHANNEL_PKG,
						    &hwmon->pl1_on_boot[CHANNEL_PKG]) |
		    xe_hwmon_pcode_read_power_limit(hwmon, PL2_HWMON_ATTR, CHANNEL_CARD,
						    &hwmon->pl2_on_boot[CHANNEL_CARD]) |
		    xe_hwmon_pcode_read_power_limit(hwmon, PL2_HWMON_ATTR, CHANNEL_PKG,
						    &hwmon->pl2_on_boot[CHANNEL_PKG])) {
			drm_warn(&hwmon->xe->drm,
				 "Failed to read power limits, check GPU firmware !\n");
		} else {
			drm_info(&hwmon->xe->drm, "Using mailbox commands for power limits\n");
			/* Write default limits to read from pcode from now on. */
			xe_hwmon_pcode_rmw_power_limit(hwmon, PL1_HWMON_ATTR,
						       CHANNEL_CARD, PWR_LIM | PWR_LIM_TIME,
						       hwmon->pl1_on_boot[CHANNEL_CARD]);
			xe_hwmon_pcode_rmw_power_limit(hwmon, PL1_HWMON_ATTR,
						       CHANNEL_PKG, PWR_LIM | PWR_LIM_TIME,
						       hwmon->pl1_on_boot[CHANNEL_PKG]);
			xe_hwmon_pcode_rmw_power_limit(hwmon, PL2_HWMON_ATTR,
						       CHANNEL_CARD, PWR_LIM | PWR_LIM_TIME,
						       hwmon->pl2_on_boot[CHANNEL_CARD]);
			xe_hwmon_pcode_rmw_power_limit(hwmon, PL2_HWMON_ATTR,
						       CHANNEL_PKG, PWR_LIM | PWR_LIM_TIME,
						       hwmon->pl2_on_boot[CHANNEL_PKG]);
			hwmon->scl_shift_power = PWR_UNIT;
			hwmon->scl_shift_energy = ENERGY_UNIT;
			hwmon->scl_shift_time = TIME_UNIT;
			hwmon->boot_power_limit_read = true;
		}
	} else {
		drm_info(&hwmon->xe->drm, "Using register for power limits\n");
		/*
		 * The contents of register PKG_POWER_SKU_UNIT do not change,
		 * so read it once and store the shift values.
		 */
		pkg_power_sku_unit = xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU_UNIT, 0);
		if (xe_reg_is_valid(pkg_power_sku_unit)) {
			val_sku_unit = xe_mmio_read32(mmio, pkg_power_sku_unit);
			hwmon->scl_shift_power = REG_FIELD_GET(PKG_PWR_UNIT, val_sku_unit);
			hwmon->scl_shift_energy = REG_FIELD_GET(PKG_ENERGY_UNIT, val_sku_unit);
			hwmon->scl_shift_time = REG_FIELD_GET(PKG_TIME_UNIT, val_sku_unit);
		}
	}
	/*
	 * Initialize 'struct xe_hwmon_energy_info', i.e. set fields to the
	 * first value of the energy register read
	 */
	for (channel = 0; channel < CHANNEL_MAX; channel++)
		if (xe_hwmon_is_visible(hwmon, hwmon_energy, hwmon_energy_input, channel))
			xe_hwmon_energy_get(hwmon, channel, &energy);

	/* Initialize 'struct xe_hwmon_fan_info' with initial fan register reading. */
	for (channel = 0; channel < FAN_MAX; channel++)
		if (xe_hwmon_is_visible(hwmon, hwmon_fan, hwmon_fan_input, channel))
			xe_hwmon_fan_input_read(hwmon, channel, &fan_speed);
}

static void xe_hwmon_mutex_destroy(void *arg)
{
	struct xe_hwmon *hwmon = arg;

	mutex_destroy(&hwmon->hwmon_lock);
}

int xe_hwmon_register(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	struct xe_hwmon *hwmon;
	int ret;

	/* hwmon is available only for dGfx */
	if (!IS_DGFX(xe))
		return 0;

	/* hwmon is not available on VFs */
	if (IS_SRIOV_VF(xe))
		return 0;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	mutex_init(&hwmon->hwmon_lock);
	ret = devm_add_action_or_reset(dev, xe_hwmon_mutex_destroy, hwmon);
	if (ret)
		return ret;

	/* There's only one instance of hwmon per device */
	hwmon->xe = xe;
	xe->hwmon = hwmon;

	xe_hwmon_get_preregistration_info(hwmon);

	drm_dbg(&xe->drm, "Register xe hwmon interface\n");

	/*  hwmon_dev points to device hwmon<i> */
	hwmon->hwmon_dev = devm_hwmon_device_register_with_info(dev, "xe", hwmon,
								&hwmon_chip_info,
								hwmon_groups);
	if (IS_ERR(hwmon->hwmon_dev)) {
		drm_err(&xe->drm, "Failed to register xe hwmon (%pe)\n", hwmon->hwmon_dev);
		xe->hwmon = NULL;
		return PTR_ERR(hwmon->hwmon_dev);
	}

	return 0;
}
MODULE_IMPORT_NS("INTEL_PMT_TELEMETRY");
