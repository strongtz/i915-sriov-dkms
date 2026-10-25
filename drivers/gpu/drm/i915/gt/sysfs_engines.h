/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_SYSFS_H
#define INTEL_ENGINE_SYSFS_H

struct drm_i915_private;
struct intel_engine_cs;

void intel_engines_add_sysfs(struct drm_i915_private *i915);
int intel_engine_add_single_sysfs(struct intel_engine_cs *engine);

#endif /* INTEL_ENGINE_SYSFS_H */
