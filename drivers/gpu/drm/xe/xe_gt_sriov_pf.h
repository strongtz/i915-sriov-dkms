/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_H_
#define _XE_GT_SRIOV_PF_H_

struct xe_gt;

#ifdef CONFIG_PCI_IOV
int xe_gt_sriov_pf_init_early(struct xe_gt *gt);
int xe_gt_sriov_pf_init(struct xe_gt *gt);
int xe_gt_sriov_pf_wait_ready(struct xe_gt *gt);
void xe_gt_sriov_pf_init_hw(struct xe_gt *gt);
void xe_gt_sriov_pf_sanitize_hw(struct xe_gt *gt, unsigned int vfid);
void xe_gt_sriov_pf_stop_prepare(struct xe_gt *gt);
void xe_gt_sriov_pf_restart(struct xe_gt *gt);
#else
static inline int xe_gt_sriov_pf_init_early(struct xe_gt *gt)
{
	return 0;
}

static inline int xe_gt_sriov_pf_init(struct xe_gt *gt)
{
	return 0;
}

static inline void xe_gt_sriov_pf_init_hw(struct xe_gt *gt)
{
}

static inline void xe_gt_sriov_pf_stop_prepare(struct xe_gt *gt)
{
}

static inline void xe_gt_sriov_pf_restart(struct xe_gt *gt)
{
}
#endif

#endif
