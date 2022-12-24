/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_SRIOV_SYSFS_TYPES_H__
#define __I915_SRIOV_SYSFS_TYPES_H__

#include <linux/kobject.h>

struct drm_i915_private;

struct i915_sriov_kobj {
	struct kobject base;
};
#define to_sriov_kobj(x) container_of(x, struct i915_sriov_kobj, base)

struct i915_sriov_attr {
	struct attribute attr;
	ssize_t (*show)(struct drm_i915_private *i915, char *buf);
	ssize_t (*store)(struct drm_i915_private *i915, const char *buf, size_t count);
};
#define to_sriov_attr(x) container_of(x, struct i915_sriov_attr, attr)

#define I915_SRIOV_ATTR(name) \
static struct i915_sriov_attr name##_sriov_attr = \
	__ATTR(name, 0644, name##_sriov_attr_show, name##_sriov_attr_store)

#define I915_SRIOV_ATTR_RO(name) \
static struct i915_sriov_attr name##_sriov_attr = \
	__ATTR(name, 0444, name##_sriov_attr_show, NULL)

struct i915_sriov_ext_kobj {
	struct kobject base;
	unsigned int id;
};
#define to_sriov_ext_kobj(x) container_of(x, struct i915_sriov_ext_kobj, base)

struct i915_sriov_ext_attr {
	struct attribute attr;
	ssize_t (*show)(struct drm_i915_private *i915, unsigned int id, char *buf);
	ssize_t (*store)(struct drm_i915_private *i915, unsigned int id,
			 const char *buf, size_t count);
};
#define to_sriov_ext_attr(x) container_of(x, struct i915_sriov_ext_attr, attr)

#define I915_SRIOV_EXT_ATTR(name) \
static struct i915_sriov_ext_attr name##_sriov_ext_attr = \
	__ATTR(name, 0644, name##_sriov_ext_attr_show, name##_sriov_ext_attr_store)

#define I915_SRIOV_EXT_ATTR_RO(name) \
static struct i915_sriov_ext_attr name##_sriov_ext_attr = \
	__ATTR(name, 0644, name##_sriov_ext_attr_show, NULL)

#define I915_SRIOV_EXT_ATTR_WO(name) \
static struct i915_sriov_ext_attr name##_sriov_ext_attr = \
	__ATTR(name, 0644, NULL, name##_sriov_ext_attr_store)

#endif /* __I915_SRIOV_SYSFS_TYPES_H__ */
