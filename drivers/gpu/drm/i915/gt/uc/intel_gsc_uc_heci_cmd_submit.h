/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _INTEL_GSC_UC_HECI_CMD_H_
#define _INTEL_GSC_UC_HECI_CMD_H_

#include <linux/types.h>

struct i915_vma;
struct intel_context;
struct intel_gsc_uc;

struct intel_gsc_mtl_header {
	u32 validity_marker;
#define GSC_HECI_VALIDITY_MARKER 0xA578875A

	u8 heci_client_id;
#define GSC_HECI_MEADDRESS_PXP 17
#define GSC_HECI_MEADDRESS_HDCP 18

	u8 reserved1;

	u16 header_version;
#define MTL_GSC_HECI_HEADER_VERSION 1

	/*
	 * The HW specs allows software to define the host_session_handle
	 * however it likes as long as its unique per user-process and usage
	 * session. Here we can use the upper bits to differentiate PXP
	 * from HDCP usages (providing some debuggability ease) while the
	 * caller provides the rest of the bits
	 */
	u64 host_session_handle;
#define GSC_HECI_HOST_SESSION_USAGE_MASK REG_GENMASK64(63, 60)
#define GSC_HECI_SESSION_PXP_SINGLE BIT_ULL(60)

	u64 gsc_message_handle;

	u32 message_size; /* lower 20 bits only, upper 12 are reserved */

	/*
	 * Flags mask:
	 * Bit 0: Pending
	 * Bit 1: Session Cleanup;
	 * Bits 2-15: Flags
	 * Bits 16-31: Extension Size
	 */
	u32 flags;
#define GSC_HECI_FLAG_MSG_PENDING	BIT(0)
#define GSC_HECI_FLAG_MSG_CLEANUP	BIT(1)

	u32 status;
} __packed;

struct intel_gsc_heci_non_priv_pkt {
	u64 addr_in;
	u32 size_in;
	u64 addr_out;
	u32 size_out;
	struct i915_vma *heci_pkt_vma;
	struct i915_vma *bb_vma;
};

void
intel_gsc_uc_heci_cmd_emit_mtl_header(struct intel_gsc_mtl_header *header,
				      u8 heci_client_id, u32 msg_size,
				      u64 host_session_id);

int
intel_gsc_uc_heci_cmd_submit_nonpriv(struct intel_gsc_uc *gsc,
				     struct intel_context *ce,
				     struct intel_gsc_heci_non_priv_pkt *pkt,
				     u32 *cs, int timeout_ms);
int intel_gsc_uc_heci_cmd_submit_packet(struct intel_gsc_uc *gsc,
					u64 addr_in, u32 size_in,
					u64 addr_out, u32 size_out);
#endif
