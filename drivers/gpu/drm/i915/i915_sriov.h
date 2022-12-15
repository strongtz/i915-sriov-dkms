/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_SRIOV_H__
#define __I915_SRIOV_H__

#include "i915_drv.h"
#include "i915_virtualization.h"

struct drm_i915_private;
struct drm_printer;

#ifdef CONFIG_PCI_IOV
#define IS_SRIOV_PF(i915) (IOV_MODE(i915) == I915_IOV_MODE_SRIOV_PF)
#else
#define IS_SRIOV_PF(i915) false
#endif
#define IS_SRIOV_VF(i915) (IOV_MODE(i915) == I915_IOV_MODE_SRIOV_VF)

#define IS_SRIOV(i915) (IS_SRIOV_PF(i915) || IS_SRIOV_VF(i915))

enum i915_iov_mode i915_sriov_probe(struct drm_i915_private *i915);
int i915_sriov_early_tweaks(struct drm_i915_private *i915);
void i915_sriov_print_info(struct drm_i915_private *i915, struct drm_printer *p);

/* PF only */
void i915_sriov_pf_confirm(struct drm_i915_private *i915);
void i915_sriov_pf_abort(struct drm_i915_private *i915, int err);
bool i915_sriov_pf_aborted(struct drm_i915_private *i915);
int i915_sriov_pf_status(struct drm_i915_private *i915);
int i915_sriov_pf_get_device_totalvfs(struct drm_i915_private *i915);
int i915_sriov_pf_get_totalvfs(struct drm_i915_private *i915);
int i915_sriov_pf_enable_vfs(struct drm_i915_private *i915, int numvfs);
int i915_sriov_pf_disable_vfs(struct drm_i915_private *i915);

bool i915_sriov_pf_is_auto_provisioning_enabled(struct drm_i915_private *i915);
int i915_sriov_pf_set_auto_provisioning(struct drm_i915_private *i915, bool enable);

/* VF only */
void i915_sriov_vf_start_migration_recovery(struct drm_i915_private *i915);

#endif /* __I915_SRIOV_H__ */
