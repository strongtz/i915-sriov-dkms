// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#define SELFTEST_RELAY_ID	0x76543210
#define SELFTEST_RELAY_DATA	0xDDDAAAA0

#define MSG_PF2GUC_RELAY_TO_VF(N)							\
	FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |				\
	FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |				\
	FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_RELAY_TO_VF),	\
	FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, VFID(N)),			\
	FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, SELFTEST_RELAY_ID)	\
	/* ...     PF2GUC_RELAY_TO_VF_REQUEST_MSG_n_RELAY_DATAx */

#define MSG_GUC2PF_RELAY_FROM_VF(N)							\
	FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |				\
	FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |				\
	FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2PF_RELAY_FROM_VF),	\
	FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, VFID(N)),			\
	FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID, SELFTEST_RELAY_ID)	\
	/* ...     GUC2PF_RELAY_FROM_VF_EVENT_MSG_n_RELAY_DATAx */

#define MSG_GUC2VF_RELAY_FROM_PF							\
	FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |				\
	FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |				\
	FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2VF_RELAY_FROM_PF),	\
	FIELD_PREP(GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID, SELFTEST_RELAY_ID)	\
	/* ...     GUC2VF_RELAY_FROM_PF_EVENT_MSG_n_RELAY_DATAx */

#define MSG_VF2GUC_RELAY_TO_PF								\
	FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |				\
	FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |				\
	FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_RELAY_TO_PF),	\
	FIELD_PREP(VF2GUC_RELAY_TO_PF_REQUEST_MSG_1_RELAY_ID, SELFTEST_RELAY_ID)	\
	/* ...     VF2GUC_RELAY_TO_PF_REQUEST_MSG_n_RELAY_DATAx */

#define MSG_IOV_SELFTEST_RELAY(OPCODE)							\
	FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |				\
	FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |				\
	FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY) |		\
	FIELD_PREP(GUC_HXG_REQUEST_MSG_0_DATA0, (OPCODE))				\
	/* ...     GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA */

#define MSG_IOV_SELFTEST_RELAY_EVENT(OPCODE)						\
	FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |				\
	FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |				\
	FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY) |		\
	FIELD_PREP(GUC_HXG_EVENT_MSG_0_DATA0, (OPCODE))					\
	/* ...     GUC_HXG_EVENT_MSG_n_DATAn, SELFTEST_RELAY_DATA */

static int relay_selftest_process_msg(struct intel_iov_relay *relay, u32 origin,
				      u32 relay_id, const u32 *msg, u32 len)
{
	/* during selftests we do allow empty relay message */
	if (unlikely(len < GUC_HXG_MSG_MIN_LEN))
		return 0;

	/* but it still has to be H2H */
	if (FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_HOST)
		return -EPROTO;

	/* only (FAST)REQUEST/EVENT are supported */
	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_REQUEST &&
	    FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_FAST_REQUEST &&
	    FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT)
		return -ENOTTY;

	/* only our action */
	if (FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]) != IOV_ACTION_SELFTEST_RELAY)
		return -ENOTTY;

	RELAY_DEBUG(relay, "received selftest %s.%u from %u = opcode %u\n",
		    hxg_type_to_string(GUC_HXG_TYPE_REQUEST), relay_id, origin,
		    FIELD_GET(GUC_HXG_REQUEST_MSG_0_DATA0, msg[0]));

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_REQUEST)
		return 0;

	if (origin) {
		if (intel_iov_is_pf(relay_to_iov(relay)))
			return intel_iov_relay_reply_ack_to_vf(relay, origin, relay_id, 0);
		return -EPROTO;
	}

	return relay_send_success(relay, origin, relay_id, 0);
}

static int relay_selftest_guc_send_nb(struct intel_guc *guc, const u32 *msg, u32 len, u32 g2h)
{
	struct intel_iov_relay *relay = &guc_to_gt(guc)->iov.relay;

	if (unlikely(!IS_ERR_OR_NULL(relay->selftest.host2guc))) {
		int ret = relay->selftest.host2guc(relay, msg, len);

		if (ret != -ENOTTY) {
			relay->selftest.host2guc = ERR_PTR(ret < 0 ? ret : 0);
			return ret;
		}
	}

	return intel_guc_send_nb(guc, msg, len, g2h);
}
