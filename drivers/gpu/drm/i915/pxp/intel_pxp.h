/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_H__
#define __INTEL_PXP_H__

#include <linux/errno.h>
#include <linux/types.h>

struct drm_device;
struct drm_file;

struct drm_i915_gem_object;
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
void intel_pxp_tee_end_fw_sessions(struct intel_pxp *pxp, u32 sessions_mask);

int intel_pxp_get_readiness_status(struct intel_pxp *pxp, int timeout_ms);
int intel_pxp_get_backend_timeout_ms(struct intel_pxp *pxp);
int intel_pxp_start(struct intel_pxp *pxp);
void intel_pxp_end(struct intel_pxp *pxp);

int intel_pxp_key_check(struct intel_pxp *pxp,
			struct drm_i915_gem_object *obj,
			bool assign);

void intel_pxp_invalidate(struct intel_pxp *pxp);
int i915_pxp_ops_ioctl(struct drm_device *dev, void *data, struct drm_file *drmfile);
void intel_pxp_close(struct intel_pxp *pxp, struct drm_file *drmfile);

#endif /* __INTEL_PXP_H__ */
