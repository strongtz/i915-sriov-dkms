// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include "gt/intel_gt.h"
#include "gt/intel_gt_regs.h"
#include "i915_drv.h"
#include "intel_pagefault.h"

struct page_fault_info {
	u8 access_type;
	u8 fault_type;
	u8 engine_id;
	u8 source_id;
	u8 fault_lvl;
	u64 address;
};

int intel_pagefault_process_cat_error_msg(struct intel_guc *guc,
					  const u32 *payload, u32 len)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
	u32 ctx_id;

	if (len < 1)
		return -EPROTO;

	ctx_id = payload[0];

	drm_err(&i915->drm, "GPU catastrophic memory error: GuC context 0x%x\n", ctx_id);

	return 0;
}

static u64 __get_address(u32 fault_data0, u32 fault_data1)
{
	return ((u64)(fault_data1 & FAULT_VA_HIGH_BITS) << 44) |
	       ((u64)fault_data0 << 12);
}

static u8 __get_engine_id(u32 fault_reg_data)
{
	return GEN8_RING_FAULT_ENGINE_ID(fault_reg_data);
}

static u8 __get_source_id(u32 fault_reg_data)
{
	return RING_FAULT_SRCID(fault_reg_data);
}

static u8 __get_access_type(u32 fault_reg_data)
{
	return !!(fault_reg_data & GEN12_RING_FAULT_ACCESS_TYPE);
}

static u8 __get_fault_lvl(u32 fault_reg_data)
{
	return RING_FAULT_LEVEL(fault_reg_data);
}

static u8 __get_fault_type(u32 fault_reg_data)
{
	return GEN12_RING_FAULT_FAULT_TYPE(fault_reg_data);
}

static void print_page_fault(struct drm_printer *p,
			     struct page_fault_info *info)
{
	drm_printf(p, "Unexpected fault\n"
		      "\tAddr: 0x%08x_%08x\n"
		      "\tEngine ID: %d\n"
		      "\tSource ID: %d\n"
		      "\tType: %d\n"
		      "\tFault Level: %d\n"
		      "\tAccess type: %s\n",
		      upper_32_bits(info->address),
		      lower_32_bits(info->address),
		      info->engine_id,
		      info->source_id,
		      info->fault_type,
		      info->fault_lvl,
		      info->access_type ?
		      "Write" : "Read");
}

/*
 * DOC: INTEL_GUC_ACTION_PAGE_FAULT_NOTIFICATION
 *
 *      +==========================================================+
 *      | G2H REPORT PAGE FAULT MESSAGE PAYLOAD                    |
 *      +==========================================================+
 *      | 0 | 31:30 |Fault response:                               |
 *      |   |       | 00 - fault successful resolved               |
 *      |   |       | 01 - fault resolution is unsuccessful        |
 *      |   |-------+----------------------------------------------|
 *      |   | 29:20 |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   | 19:18 |Fault type:                                   |
 *      |   |       | 00 - page not present                        |
 *      |   |       | 01 - write access violation                  |
 *      |   |-------+----------------------------------------------|
 *      |   |   17  |Access type of the memory request that fault  |
 *      |   |       | 0 - faulted access is a read request         |
 *      |   |       | 1 = faulted access is a write request        |
 *      |   |-------+----------------------------------------------|
 *      |   | 16:12 |Engine Id of the faulted memory cycle         |
 *      |   |-------+----------------------------------------------|
 *      |   |   11  |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   |  10:3 |Source ID of the faulted memory cycle         |
 *      |   |-------+----------------------------------------------|
 *      |   |   2:1 |Fault level:                                  |
 *      |   |       | 00 - PTE                                     |
 *      |   |       | 01 - PDE                                     |
 *      |   |       | 10 - PDP                                     |
 *      |   |       | 11 - PML4                                    |
 *      |   |-------+----------------------------------------------|
 *      |   |     0 |Valid bit                                     |
 *      +---+-------+----------------------------------------------+
 *      | 1 |  31:0 |Fault cycle virtual address [43:12]           |
 *      +---+-------+----------------------------------------------+
 *      | 2 |  31:4 |Reserved                                      |
 *      |   |-------+----------------------------------------------|
 *      |   |   3:0 |Fault cycle virtual address [47:44]           |
 *      +==========================================================+
 */
int intel_pagefault_process_page_fault_msg(struct intel_guc *guc,
					   const u32 *payload, u32 len)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct page_fault_info info = {};
	struct drm_printer p = drm_info_printer(i915->drm.dev);

	if (len < 3)
		return -EPROTO;

	info.address = __get_address(payload[1], payload[2]);
	info.engine_id = __get_engine_id(payload[0]);
	info.source_id = __get_source_id(payload[0]);
	info.access_type = __get_access_type(payload[0]);
	info.fault_lvl = __get_fault_lvl(payload[0]);
	info.fault_type = __get_fault_type(payload[0]);

	print_page_fault(&p, &info);

	return 0;
}
