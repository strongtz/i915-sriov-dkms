/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_HWMON_H__
#define __INTEL_HWMON_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include "i915_reg.h"

struct drm_i915_private;

struct i915_hwmon_reg {
	i915_reg_t pkg_power_sku_unit;
	i915_reg_t pkg_power_sku;
	i915_reg_t pkg_rapl_limit;
	i915_reg_t energy_status_all;
	i915_reg_t energy_status_tile;
};

struct i915_energy_info {
	u32 energy_counter_overflow;
	u32 energy_counter_prev;
};

struct i915_hwmon_drvdata {
	struct i915_hwmon *dd_hwmon;
	struct intel_uncore *dd_uncore;
	struct device *dd_hwmon_dev;
	struct i915_energy_info dd_ei;	/*  Energy info for energy1_input */
	char dd_name[12];
};

struct i915_hwmon {
	struct i915_hwmon_drvdata ddat;

	struct mutex hwmon_lock;	/* counter overflow logic and rmw */

	struct i915_hwmon_reg rg;

	u32 power_max_initial_value;

	int scl_shift_power;
	int scl_shift_energy;
};

void i915_hwmon_register(struct drm_i915_private *i915);
void i915_hwmon_unregister(struct drm_i915_private *i915);

int i915_energy_status_get(struct drm_i915_private *i915, u64 *energy);
#endif
