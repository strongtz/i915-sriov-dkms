/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_SERVICE_TYPES_H_
#define _XE_GT_SRIOV_PF_SERVICE_TYPES_H_

#include <linux/types.h>

struct xe_reg;

/**
 * struct xe_gt_sriov_pf_service_version - VF/PF ABI Version.
 * @major: the major version of the VF/PF ABI
 * @minor: the minor version of the VF/PF ABI
 *
 * See `GuC Relay Communication`_.
 */
struct xe_gt_sriov_pf_service_version {
	u16 major;
	u16 minor;
};

/**
 * struct xe_gt_sriov_pf_service_runtime_regs - Runtime data shared with VFs.
 * @regs: pointer to static array with register offsets.
 * @values: pointer to array with captured register values.
 * @size: size of the regs and value arrays.
 */
struct xe_gt_sriov_pf_service_runtime_regs {
	const struct xe_reg *regs;
	u32 *values;
	u32 size;
};

#define XE_SRIOV_RELAY_TRACE_DETAIL_LEN 64

struct xe_gt_sriov_pf_relay_trace_entry {
	u64 ts_ns;
	u32 opcode;
	u32 magic;
	u32 msg[4];
};

struct xe_gt_sriov_pf_relay_trace_vf {
	u32 relay_actions;
	u64 relay_actions_full[4];
	u32 last_action;
	u32 last_data;
	u64 last_action_ts_ns;

	u32 mmio_opcodes;
	u64 mmio_opcodes_full[4];
	u32 last_mmio_opcode;
	u32 last_magic;
	u32 last_msg[4];
	u64 last_mmio_ts_ns;

	u8 mmio_handshake;
	u8 mmio_runtime;
	u8 ggtt_no_handshake;

	struct {
		struct xe_gt_sriov_pf_relay_trace_entry entries[XE_SRIOV_RELAY_TRACE_DETAIL_LEN];
		u32 head;
		u32 count;
	} detail;
};

/**
 * struct xe_gt_sriov_pf_service - Data used by the PF service.
 * @version: information about VF/PF ABI versions for current platform.
 * @version.base: lowest VF/PF ABI version that could be negotiated with VF.
 * @version.latest: latest VF/PF ABI version supported by the PF driver.
 * @runtime: runtime data shared with VFs.
 * @relay_trace: per-VF relay trace data (PF only).
 * @relay_trace_num_vfs: number of elements in @relay_trace (VF count).
 */
struct xe_gt_sriov_pf_service {
	struct {
		struct xe_gt_sriov_pf_service_version base;
		struct xe_gt_sriov_pf_service_version latest;
	} version;
	struct xe_gt_sriov_pf_service_runtime_regs runtime;
	struct xe_gt_sriov_pf_relay_trace_vf *relay_trace;
	u32 relay_trace_num_vfs;
};

#endif
