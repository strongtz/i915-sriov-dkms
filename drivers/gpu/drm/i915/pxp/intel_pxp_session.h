/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_SESSION_H__
#define __INTEL_PXP_SESSION_H__

#include <linux/types.h>

struct drm_file;
struct intel_pxp;
struct work_struct;

#define ARB_SESSION I915_PROTECTED_CONTENT_DEFAULT_SESSION /* shorter define */

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_session_management_fini(struct intel_pxp *pxp);
void intel_pxp_session_management_init(struct intel_pxp *pxp);
void intel_pxp_terminate(struct intel_pxp *pxp, bool post_invalidation_needs_restart);
int intel_pxp_sm_ioctl_reserve_session(struct intel_pxp *pxp, struct drm_file *drmfile,
				       int protection_mode, u32 *pxp_tag);
int intel_pxp_sm_ioctl_mark_session_in_play(struct intel_pxp *pxp,
					    struct drm_file *drmfile,
					    u32 session_id);
int intel_pxp_sm_ioctl_terminate_session(struct intel_pxp *pxp,
					 struct drm_file *drmfile,
					 u32 session_id);

int intel_pxp_sm_ioctl_query_pxp_tag(struct intel_pxp *pxp,
				     u32 *session_is_alive, u32 *pxp_tag);

void intel_pxp_file_close(struct intel_pxp *pxp, struct drm_file *drmfile);

bool intel_pxp_session_is_in_play(struct intel_pxp *pxp, u32 id);

#else
static inline void intel_pxp_session_management_init(struct intel_pxp *pxp)
{
}

static inline void intel_pxp_session_management_fini(struct intel_pxp *pxp)
{
}

static inline void intel_pxp_terminate(struct intel_pxp *pxp, bool post_invalidation_needs_restart)
{
}

static inline int intel_pxp_sm_ioctl_reserve_session(struct intel_pxp *pxp,
						     struct drm_file *drmfile,
						     int protection_mode,
						     u32 *pxp_tag)
{
	return 0;
}

static inline int intel_pxp_sm_ioctl_mark_session_in_play(struct intel_pxp *pxp,
							  struct drm_file *drmfile,
							  u32 session_id)
{
	return 0;
}

static inline int intel_pxp_sm_ioctl_terminate_session(struct intel_pxp *pxp,
						       struct drm_file *drmfile,
						       u32 session_id)
{
	return 0;
}

static inline bool intel_pxp_session_is_in_play(struct intel_pxp *pxp, u32 id)
{
	return false;
}

static inline int intel_pxp_sm_ioctl_query_pxp_tag(struct intel_pxp *pxp,
						   u32 *session_is_alive, u32 *pxp_tag)
{
	return 0;
}
#endif

#endif /* __INTEL_PXP_SESSION_H__ */
