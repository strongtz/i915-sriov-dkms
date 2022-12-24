/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_RELAY_H__
#define __INTEL_IOV_RELAY_H__

#include "intel_iov_types.h"

static inline void intel_iov_relay_init_early(struct intel_iov_relay *relay)
{
	spin_lock_init(&relay->lock);
	INIT_LIST_HEAD(&relay->pending_relays);
}

int intel_iov_relay_send_to_vf(struct intel_iov_relay *relay, u32 target,
			       const u32 *msg, u32 len, u32 *buf, u32 buf_size);
int intel_iov_relay_reply_to_vf(struct intel_iov_relay *relay, u32 target,
				u32 relay_id, const u32 *msg, u32 len);
int intel_iov_relay_reply_ack_to_vf(struct intel_iov_relay *relay, u32 target,
				    u32 relay_id, u32 data);
int intel_iov_relay_reply_err_to_vf(struct intel_iov_relay *relay, u32 target,
				    u32 relay_id, int err);
int intel_iov_relay_reply_error_to_vf(struct intel_iov_relay *relay, u32 target,
				      u32 relay_id, u16 error, u16 hint);

int intel_iov_relay_send_to_pf(struct intel_iov_relay *relay,
			       const u32 *msg, u32 len, u32 *buf, u32 buf_size);

int intel_iov_relay_process_guc2pf(struct intel_iov_relay *relay,
				   const u32 *msg, u32 len);
int intel_iov_relay_process_guc2vf(struct intel_iov_relay *relay,
				   const u32 *msg, u32 len);

#endif /* __INTEL_IOV_RELAY_H__ */
