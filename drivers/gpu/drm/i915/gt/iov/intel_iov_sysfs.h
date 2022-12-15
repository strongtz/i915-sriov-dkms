/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_SYSFS_H__
#define __INTEL_IOV_SYSFS_H__

struct intel_iov;

int intel_iov_sysfs_setup(struct intel_iov *iov);
void intel_iov_sysfs_teardown(struct intel_iov *iov);

#endif /* __INTEL_IOV_SYSFS_H__ */
