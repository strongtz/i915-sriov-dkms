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
 * @sysfs: FIXME missing doc
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
 * struct i915_sriov - i915 SR-IOV data.
 * @pf: PF only data.
 */
struct i915_sriov {
	struct i915_sriov_pf pf;
};

#endif /* __I915_SRIOV_TYPES_H__ */
