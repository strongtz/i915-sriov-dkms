/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_PAGEFAULT_H
#define _INTEL_PAGEFAULT_H

#include <linux/types.h>

struct intel_guc;

int intel_pagefault_process_cat_error_msg(struct intel_guc *guc,
					  const u32 *payload, u32 len);
int intel_pagefault_process_page_fault_msg(struct intel_guc *guc,
					   const u32 *payload, u32 len);
#endif
