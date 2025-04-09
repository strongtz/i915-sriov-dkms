/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_H__
#define __INTEL_PXP_H__

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
struct drm_i915_gem_object;
#else
struct drm_gem_object;
#endif
struct drm_i915_private;
struct intel_pxp;

bool intel_pxp_is_supported(const struct intel_pxp *pxp);
bool intel_pxp_is_enabled(const struct intel_pxp *pxp);
bool intel_pxp_is_active(const struct intel_pxp *pxp);

int intel_pxp_init(struct drm_i915_private *i915);
void intel_pxp_fini(struct drm_i915_private *i915);

void intel_pxp_init_hw(struct intel_pxp *pxp);
void intel_pxp_fini_hw(struct intel_pxp *pxp);

void intel_pxp_mark_termination_in_progress(struct intel_pxp *pxp);
void intel_pxp_tee_end_arb_fw_session(struct intel_pxp *pxp, u32 arb_session_id);

int intel_pxp_get_readiness_status(struct intel_pxp *pxp, int timeout_ms);
int intel_pxp_get_backend_timeout_ms(struct intel_pxp *pxp);
int intel_pxp_start(struct intel_pxp *pxp);
void intel_pxp_end(struct intel_pxp *pxp);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
int intel_pxp_key_check(struct intel_pxp *pxp,
			struct drm_i915_gem_object *obj,
			bool assign);
#else
int intel_pxp_key_check(struct intel_pxp *pxp,
			struct drm_gem_object *obj,
			bool assign);
#endif

void intel_pxp_invalidate(struct intel_pxp *pxp);

#endif /* __INTEL_PXP_H__ */
