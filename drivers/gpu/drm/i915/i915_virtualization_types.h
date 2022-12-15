/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_VIRTUALIZATION_TYPES_H__
#define __I915_VIRTUALIZATION_TYPES_H__

/**
 * enum i915_iov_mode - I/O Virtualization mode.
 */
enum i915_iov_mode {
	I915_IOV_MODE_NONE = 1,
	I915_IOV_MODE_GVT_VGPU,
	I915_IOV_MODE_SRIOV_PF,
	I915_IOV_MODE_SRIOV_VF,
};

#endif /* __I915_VIRTUALIZATION_TYPES_H__ */
