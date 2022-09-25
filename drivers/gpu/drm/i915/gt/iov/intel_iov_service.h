/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_SERVICE_H__
#define __INTEL_IOV_SERVICE_H__

#include <linux/types.h>
#include <linux/errno.h>

struct intel_iov;

void intel_iov_service_init_early(struct intel_iov *iov);
void intel_iov_service_update(struct intel_iov *iov);
void intel_iov_service_reset(struct intel_iov *iov);
void intel_iov_service_release(struct intel_iov *iov);

int intel_iov_service_process_msg(struct intel_iov *iov, u32 origin,
				  u32 relay_id, const u32 *msg, u32 len);

int intel_iov_service_process_mmio_relay(struct intel_iov *iov, const u32 *msg,
					 u32 len);

#endif /* __INTEL_IOV_SERVICE_H__ */
