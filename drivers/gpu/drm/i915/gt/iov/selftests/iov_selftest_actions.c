// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gt/iov/abi/iov_actions_selftest_abi.h"
#include "gt/iov/intel_iov_utils.h"
#include "gt/iov/intel_iov_relay.h"
#include "iov_selftest_actions.h"

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
/**
 * intel_iov_selftest_send_vfpf_get_ggtt_pte - Get the PTE value from PF.
 * @iov: the IOV struct
 * @ggtt_addr: GGTT address
 * @pte: pointer to PTE value
 *
 * The function will get the PTE value from PF using VFPF debug communication.
 *
 * This function can only be called on VF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_selftest_send_vfpf_get_ggtt_pte(struct intel_iov *iov, u64 ggtt_addr, u64 *pte)
{
	u32 request[VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_LEN];
	u32 response[VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_LEN];
	u32 pte_lo, pte_hi;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	request[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		     FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		     FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_PF_ST_ACTION) |
		     FIELD_PREP(VF2PF_PF_ST_ACTION_REQUEST_MSG_0_OPCODE,
				IOV_OPCODE_ST_GET_GGTT_PTE);
	request[1] = FIELD_PREP(VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_1_ADDRESS_LO,
				lower_32_bits(ggtt_addr));
	request[2] = FIELD_PREP(VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_2_ADDRESS_HI,
				upper_32_bits(ggtt_addr));

	err = intel_iov_relay_send_to_pf(&iov->relay,
					 request, ARRAY_SIZE(request),
					 response, ARRAY_SIZE(response));

	if (err < 0) {
		IOV_ERROR(iov, "ST: failed to get PTE value for %#llx, %d\n", ggtt_addr, err);
		return err;
	}

	pte_lo = FIELD_GET(VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_1_PTE_LO, response[1]);
	pte_hi = FIELD_GET(VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_2_PTE_HI, response[2]);

	*pte = make_u64(pte_hi, pte_lo);

	return err;
}

static int pf_handle_action_get_ggtt_pte(struct intel_iov *iov, u32 origin, u32 relay_id,
					 const u32 *msg, u32 len)
{
	u32 response[VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_LEN];
	u32 addr_lo, addr_hi;
	u64 ggtt_addr;
	void __iomem *pte_addr;
	gen8_pte_t pte;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (unlikely(len != VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_LEN))
		return -EPROTO;

	addr_lo = FIELD_GET(VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_1_ADDRESS_LO, msg[1]);
	addr_hi = FIELD_GET(VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_2_ADDRESS_HI, msg[2]);

	ggtt_addr = make_u64(addr_hi, addr_lo);

	if (!IS_ALIGNED(ggtt_addr, I915_GTT_PAGE_SIZE_4K))
		return -EINVAL;

	pte_addr = iov_to_gt(iov)->ggtt->gsm + ggtt_addr_to_pte_offset(ggtt_addr);

	pte = gen8_get_pte(pte_addr);

	response[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		      FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		      FIELD_PREP(VF2PF_PF_ST_ACTION_RESPONSE_MSG_0_MBZ, 0);
	response[1] = FIELD_PREP(VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_1_PTE_LO, lower_32_bits(pte));
	response[2] = FIELD_PREP(VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_2_PTE_HI, upper_32_bits(pte));

	return intel_iov_relay_reply_to_vf(&iov->relay, origin, relay_id, response,
					   ARRAY_SIZE(response));
}

int intel_iov_service_perform_selftest_action(struct intel_iov *iov, u32 origin, u32 relay_id,
					      const u32 *msg, u32 len)
{
	u32 opcode;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (unlikely(len < VF2PF_PF_ST_ACTION_REQUEST_MSG_MIN_LEN ||
		     len > VF2PF_PF_ST_ACTION_REQUEST_MSG_MAX_LEN))
		return -EPROTO;

	opcode = FIELD_GET(VF2PF_PF_ST_ACTION_REQUEST_MSG_0_OPCODE, msg[0]);

	switch (opcode) {
	case IOV_OPCODE_ST_GET_GGTT_PTE:
		return pf_handle_action_get_ggtt_pte(iov, origin, relay_id, msg, len);
	default:
		IOV_ERROR(iov, "Unsupported selftest opcode %#x from VF%u\n", opcode, origin);
		return -EBADRQC;
	}

	return intel_iov_relay_reply_ack_to_vf(&iov->relay, origin, relay_id, 0);
}
#endif /* IS_ENABLED(CONFIG_DRM_I915_SELFTEST) */
