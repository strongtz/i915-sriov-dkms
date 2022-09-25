/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_UTILS_H__
#define __INTEL_IOV_UTILS_H__

#include "i915_drv.h"

static inline struct intel_gt *iov_to_gt(struct intel_iov *iov)
{
	return container_of(iov, struct intel_gt, iov);
}

static inline struct intel_guc *iov_to_guc(struct intel_iov *iov)
{
	return &iov_to_gt(iov)->uc.guc;
}

static inline struct drm_i915_private *iov_to_i915(struct intel_iov *iov)
{
	return iov_to_gt(iov)->i915;
}

static inline struct device *iov_to_dev(struct intel_iov *iov)
{
	return iov_to_i915(iov)->drm.dev;
}

static inline bool intel_iov_is_pf(struct intel_iov *iov)
{
	return IS_SRIOV_PF(iov_to_i915(iov));
}

static inline bool intel_iov_is_vf(struct intel_iov *iov)
{
	return IS_SRIOV_VF(iov_to_i915(iov));
}

static inline bool intel_iov_is_enabled(struct intel_iov *iov)
{
	return intel_iov_is_pf(iov) || intel_iov_is_vf(iov);
}

static inline u16 pf_get_totalvfs(struct intel_iov *iov)
{
	return i915_sriov_pf_get_totalvfs(iov_to_i915(iov));
}

static inline u16 pf_get_numvfs(struct intel_iov *iov)
{
	return pci_num_vf(to_pci_dev(iov_to_dev(iov)));
}

static inline bool pf_in_error(struct intel_iov *iov)
{
	return i915_sriov_pf_aborted(iov_to_i915(iov));
}

static inline int pf_get_status(struct intel_iov *iov)
{
	return i915_sriov_pf_status(iov_to_i915(iov));
}

static inline struct mutex *pf_provisioning_mutex(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	return &iov->pf.provisioning.lock;
}

#define IOV_ERROR(_iov, _fmt, ...) \
	drm_notice(&iov_to_i915(_iov)->drm, "IOV: " _fmt, ##__VA_ARGS__)
#define IOV_PROBE_ERROR(_iov, _fmt, ...) \
	i915_probe_error(iov_to_i915(_iov), "IOV: " _fmt, ##__VA_ARGS__)

#ifdef CONFIG_DRM_I915_DEBUG_IOV
#define IOV_DEBUG(_iov, _fmt, ...) \
	drm_dbg(&iov_to_i915(_iov)->drm, "IOV: " _fmt, ##__VA_ARGS__)
#else
#define IOV_DEBUG(_iov, _fmt, ...) typecheck(struct intel_iov *, _iov)
#endif

static inline void pf_update_status(struct intel_iov *iov, int status, const char *reason)
{
	GEM_BUG_ON(status >= 0);
	IOV_PROBE_ERROR(iov, "Initialization failed (%pe) %s\n", ERR_PTR(status), reason);
	i915_sriov_pf_abort(iov_to_i915(iov), status);
}

static inline void pf_mark_manual_provisioning(struct intel_iov *iov)
{
	i915_sriov_pf_set_auto_provisioning(iov_to_i915(iov), false);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#define IOV_SELFTEST_ERROR(_iov, _fmt, ...) \
	IOV_ERROR((_iov), "selftest/%s: " _fmt, __func__, ##__VA_ARGS__)

#define intel_iov_live_subtests(T, data) ({ \
	typecheck(struct intel_iov *, data); \
	__i915_subtests(__func__, \
			__intel_iov_live_setup, __intel_iov_live_teardown, \
			T, ARRAY_SIZE(T), data); \
})

static inline int __intel_iov_live_setup(void *data)
{
	return __intel_gt_live_setup(iov_to_gt(data));
}

static inline int __intel_iov_live_teardown(int err, void *data)
{
	return __intel_gt_live_teardown(err, iov_to_gt(data));
}
#endif /* IS_ENABLED(CONFIG_DRM_I915_SELFTEST) */

#endif /* __INTEL_IOV_UTILS_H__ */
