/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_MEMIRQ_H__
#define __INTEL_IOV_MEMIRQ_H__

struct intel_iov;

int intel_iov_memirq_init(struct intel_iov *iov);
void intel_iov_memirq_fini(struct intel_iov *iov);

int intel_iov_memirq_prepare_guc(struct intel_iov *iov);

void intel_iov_memirq_reset(struct intel_iov *iov);
void intel_iov_memirq_postinstall(struct intel_iov *iov);
void intel_iov_memirq_handler(struct intel_iov *iov);

#endif /* __INTEL_IOV_MEMIRQ_H__ */
