/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_DEBUGFS_H__
#define __INTEL_IOV_DEBUGFS_H__

struct intel_iov;
struct dentry;

void intel_iov_debugfs_register(struct intel_iov *iov, struct dentry *root);

#endif /* __INTEL_IOV_DEBUGFS_H__ */
