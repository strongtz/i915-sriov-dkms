/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_SRIOV_TYPES_H__
#define __I915_SRIOV_TYPES_H__

#include <linux/types.h>
#include "i915_sriov_sysfs_types.h"

/**
 * struct i915_sriov_pf - i915 SR-IOV PF data.
 * @__status: Status of the PF. Don't access directly!
 * @device_vfs: Number of VFs supported by the device.
 * @driver_vfs: Number of VFs supported by the driver.
 * @sysfs.home: Home object for all entries in sysfs.
 * @sysfs.kobjs: Array with PF and VFs objects exposed in sysfs.
 */
struct i915_sriov_pf {
	int __status;
	u16 device_vfs;
	u16 driver_vfs;
	struct {
		struct i915_sriov_kobj *home;
		struct i915_sriov_ext_kobj **kobjs;
	} sysfs;

	/** @disable_auto_provisioning: flag to control VFs auto-provisioning */
	bool disable_auto_provisioning;
};

/**
 * struct i915_sriov_vf - i915 SR-IOV VF data.
 */
struct i915_sriov_vf {

	/** @migration_worker: migration recovery worker */
	struct work_struct migration_worker;
};

/**
 * struct i915_sriov - i915 SR-IOV data.
 * @pf: PF only data.
 */
struct i915_sriov {
	union {
		struct i915_sriov_pf pf;
		struct i915_sriov_vf vf;
	};
};

#endif /* __I915_SRIOV_TYPES_H__ */
