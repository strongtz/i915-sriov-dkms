/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PM_H__
#define __INTEL_PM_H__

#include <linux/types.h>

#include "display/intel_global_state.h"

struct drm_i915_private;
struct intel_crtc_state;
struct intel_plane_state;

void intel_init_clock_gating(struct drm_i915_private *dev_priv);
void intel_suspend_hw(struct drm_i915_private *dev_priv);
int ilk_wm_max_level(const struct drm_i915_private *dev_priv);
void intel_init_pmdemand(struct drm_i915_private *dev_priv);
void intel_init_pm(struct drm_i915_private *dev_priv);
void intel_init_clock_gating_hooks(struct drm_i915_private *dev_priv);
void intel_pm_setup(struct drm_i915_private *dev_priv);
void g4x_wm_get_hw_state(struct drm_i915_private *dev_priv);
void vlv_wm_get_hw_state(struct drm_i915_private *dev_priv);
void ilk_wm_get_hw_state(struct drm_i915_private *dev_priv);
void g4x_wm_sanitize(struct drm_i915_private *dev_priv);
void vlv_wm_sanitize(struct drm_i915_private *dev_priv);
bool ilk_disable_lp_wm(struct drm_i915_private *dev_priv);
bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state);
void intel_print_wm_latency(struct drm_i915_private *dev_priv,
			    const char *name, const u16 wm[]);

bool intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable);

struct intel_pmdemand_state {
	struct intel_global_state base;

	u16 qclk_gv_bw;
	u8 voltage_index;
	u8 qclk_gv_index;
	u8 active_pipes;
	u8 dbufs;
	u8 active_phys_plls_mask;
	u16 cdclk_freq_mhz;
	u16 ddiclk_freq_mhz;
	u8 scalers;
};

int intel_pmdemand_init(struct drm_i915_private *dev_priv);

struct intel_pmdemand_state *
intel_atomic_get_pmdemand_state(struct intel_atomic_state *state);

#define to_intel_pmdemand_state(x) container_of((x), struct intel_pmdemand_state, base)
#define intel_atomic_get_old_pmdemand_state(state) \
	to_intel_pmdemand_state(intel_atomic_get_old_global_obj_state(state, &to_i915(state->base.dev)->pmdemand.obj))
#define intel_atomic_get_new_pmdemand_state(state) \
	to_intel_pmdemand_state(intel_atomic_get_new_global_obj_state(state, &to_i915(state->base.dev)->pmdemand.obj))

int intel_pmdemand_init(struct drm_i915_private *dev_priv);
void intel_program_dbuf_pmdemand(struct drm_i915_private *dev_priv,
				 u8 dbuf_slices);
void intel_pmdemand_pre_plane_update(struct intel_atomic_state *state);
void intel_pmdemand_post_plane_update(struct intel_atomic_state *state);
int intel_pmdemand_atomic_check(struct intel_atomic_state *state);

#endif /* __INTEL_PM_H__ */
