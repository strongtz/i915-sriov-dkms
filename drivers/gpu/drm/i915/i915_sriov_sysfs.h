/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_SRIOV_SYSFS_H__
#define __I915_SRIOV_SYSFS_H__

#include "i915_sriov_sysfs_types.h"

int i915_sriov_sysfs_setup(struct drm_i915_private *i915);
void i915_sriov_sysfs_teardown(struct drm_i915_private *i915);
void i915_sriov_sysfs_update_links(struct drm_i915_private *i915, bool add);

struct drm_i915_private *sriov_kobj_to_i915(struct i915_sriov_kobj *kobj);
struct drm_i915_private *sriov_ext_kobj_to_i915(struct i915_sriov_ext_kobj *kobj);

#endif /* __I915_SRIOV_SYSFS_H__ */
