/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _IOV_SELFTEST_ACTIONS_H_
#define _IOV_SELFTEST_ACTIONS_H_

#include <linux/errno.h>
#include <linux/types.h>

struct intel_iov;

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
int intel_iov_service_perform_selftest_action(struct intel_iov *iov, u32 origin, u32 relay_id,
					      const u32 *msg, u32 len);
int intel_iov_selftest_send_vfpf_get_ggtt_pte(struct intel_iov *iov, u64 ggtt_addr, u64 *pte);
#else
static inline int intel_iov_service_perform_selftest_action(struct intel_iov *iov, u32 origin,
							    u32 relay_id, const u32 *msg, u32 len)
{
	return -EOPNOTSUPP;
}
#endif /* IS_ENABLED(CONFIG_DRM_I915_SELFTEST) */

#endif /* _IOV_SELFTEST_ACTIONS_H_ */
