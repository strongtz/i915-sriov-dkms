/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2022, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_GSCCS_H__
#define __INTEL_PXP_GSCCS_H__

#include <linux/types.h>

#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"

struct drm_file;
struct intel_pxp;

#define GSC_PENDING_RETRY_MAXCOUNT 40
#define GSC_PENDING_RETRY_PAUSE_MS 50
#define GSCFW_MAX_ROUND_TRIP_LATENCY_MS (GSC_HECI_REPLY_LATENCY_MS + \
					 (GSC_PENDING_RETRY_MAXCOUNT * GSC_PENDING_RETRY_PAUSE_MS))

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_gsccs_fini(struct intel_pxp *pxp);
int intel_pxp_gsccs_init(struct intel_pxp *pxp);

int intel_pxp_gsccs_create_session(struct intel_pxp *pxp, int arb_session_id);

void intel_gsccs_free_client_resources(struct intel_pxp *pxp,
				       struct drm_file *drmfile);
int intel_gsccs_alloc_client_resources(struct intel_pxp *pxp,
				       struct drm_file *drmfile);
int
intel_pxp_gsccs_client_io_msg(struct intel_pxp *pxp, struct drm_file *drmfile,
			      void *msg_in, size_t msg_in_size,
			      void *msg_out, size_t msg_out_size_max,
			      u32 *msg_out_len);

void intel_pxp_gsccs_end_fw_sessions(struct intel_pxp *pxp, u32 sessions_mask);
int intel_pxp_gsccs_get_client_host_session_handle(struct intel_pxp *pxp, struct drm_file *drmfile,
						   u64 *handle);

#else
static inline void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
}

static inline int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	return 0;
}

#endif

bool intel_pxp_gsccs_is_ready_for_sessions(struct intel_pxp *pxp);

#endif /*__INTEL_PXP_GSCCS_H__ */
