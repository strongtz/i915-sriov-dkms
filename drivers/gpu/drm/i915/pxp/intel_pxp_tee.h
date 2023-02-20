/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TEE_H__
#define __INTEL_PXP_TEE_H__

#include <linux/version.h>
#include "intel_pxp.h"

int intel_pxp_tee_component_init(struct intel_pxp *pxp);
void intel_pxp_tee_component_fini(struct intel_pxp *pxp);

int intel_pxp_tee_cmd_create_arb_session(struct intel_pxp *pxp,
					 int arb_session_id);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,2,0)
int intel_pxp_tee_stream_message(struct intel_pxp *pxp,
				 u8 client_id, u32 fence_id,
				 void *msg_in, size_t msg_in_len,
				 void *msg_out, size_t msg_out_len);
#endif

#endif /* __INTEL_PXP_TEE_H__ */
