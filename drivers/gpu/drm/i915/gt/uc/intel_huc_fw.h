/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_HUC_FW_H_
#define _INTEL_HUC_FW_H_

#include <linux/version.h>

struct intel_huc;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,2,0)
int intel_huc_fw_load_and_auth_via_gsc(struct intel_huc *huc);
#endif
int intel_huc_fw_upload(struct intel_huc *huc);

#endif
