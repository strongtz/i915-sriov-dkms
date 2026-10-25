/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_GT_CCS_MODE_H__
#define __INTEL_GT_CCS_MODE_H__

#include "intel_gt.h"

void intel_gt_ccs_mode_init(struct intel_gt *gt);
void intel_gt_sysfs_ccs_init(struct intel_gt *gt);

#endif /* __INTEL_GT_CCS_MODE_H__ */
