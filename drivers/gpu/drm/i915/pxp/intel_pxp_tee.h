/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TEE_H__
#define __INTEL_PXP_TEE_H__

#include "intel_pxp.h"

struct prelim_drm_i915_pxp_tee_io_message_params;

int intel_pxp_tee_component_init(struct intel_pxp *pxp);
void intel_pxp_tee_component_fini(struct intel_pxp *pxp);

int intel_pxp_tee_cmd_create_arb_session(struct intel_pxp *pxp,
					 int arb_session_id);

int intel_pxp_tee_stream_message(struct intel_pxp *pxp,
				 u8 client_id, u32 fence_id,
				 void *msg_in, size_t msg_in_len,
				 void *msg_out, size_t msg_out_len);

int intel_pxp_tee_io_message(struct intel_pxp *pxp,
			     void *msg_in, u32 msg_in_size,
			     void *msg_out, u32 msg_out_max_size,
			     u32 *msg_out_rcv_size);

#endif /* __INTEL_PXP_TEE_H__ */
