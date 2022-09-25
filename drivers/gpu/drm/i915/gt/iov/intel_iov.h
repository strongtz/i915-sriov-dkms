/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_H__
#define __INTEL_IOV_H__

struct intel_iov;

void intel_iov_init_early(struct intel_iov *iov);
void intel_iov_release(struct intel_iov *iov);

int intel_iov_init_mmio(struct intel_iov *iov);
int intel_iov_init_ggtt(struct intel_iov *iov);
void intel_iov_fini_ggtt(struct intel_iov *iov);
int intel_iov_init(struct intel_iov *iov);
void intel_iov_fini(struct intel_iov *iov);

int intel_iov_init_hw(struct intel_iov *iov);
void intel_iov_fini_hw(struct intel_iov *iov);
int intel_iov_init_late(struct intel_iov *iov);

void intel_iov_pf_get_pm_vfs(struct intel_iov *iov);
void intel_iov_pf_put_pm_vfs(struct intel_iov *iov);

void intel_iov_suspend(struct intel_iov *iov);
void intel_iov_resume(struct intel_iov *iov);

#endif /* __INTEL_IOV_H__ */
