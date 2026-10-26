/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_STATE_H__
#define __INTEL_IOV_STATE_H__

#include <linux/types.h>

struct intel_iov;

void intel_iov_state_init_early(struct intel_iov *iov);
void intel_iov_state_release(struct intel_iov *iov);
void intel_iov_state_reset(struct intel_iov *iov);

void intel_iov_state_start_flr(struct intel_iov *iov, u32 vfid);
bool intel_iov_state_no_flr(struct intel_iov *iov, u32 vfid);

int intel_iov_state_pause_vf(struct intel_iov *iov, u32 vfid);
int intel_iov_state_pause_vf_sync(struct intel_iov *iov, u32 vfid, bool inferred);
bool intel_iov_state_no_pause(struct intel_iov *iov, u32 vfid);
int intel_iov_state_resume_vf(struct intel_iov *iov, u32 vfid);
int intel_iov_state_stop_vf(struct intel_iov *iov, u32 vfid);
int intel_iov_state_save_vf_size(struct intel_iov *iov, u32 vfid);
ssize_t intel_iov_state_save_ggtt(struct intel_iov *iov, u32 vfid, void *buf, size_t size);
int intel_iov_state_restore_ggtt(struct intel_iov *iov, u32 vfid, const void *buf, size_t size);
int intel_iov_state_save_vf(struct intel_iov *iov, u32 vfid, void *buf, size_t size);
int intel_iov_state_restore_vf(struct intel_iov *iov, u32 vfid, const void *buf, size_t size);
int intel_iov_state_store_guc_migration_state(struct intel_iov *iov, u32 vfid,
					      const void *buf, size_t size);

int intel_iov_state_process_guc2pf(struct intel_iov *iov,
				   const u32 *msg, u32 len);

#endif /* __INTEL_IOV_STATE_H__ */
