/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_MIGRATION_H__
#define __INTEL_IOV_MIGRATION_H__

struct intel_iov;

int intel_iov_migration_reinit_guc(struct intel_iov *iov);
void intel_iov_migration_fixup_ggtt_nodes(struct intel_iov *iov);

#endif /* __INTEL_IOV_MIGRATION_H__ */
