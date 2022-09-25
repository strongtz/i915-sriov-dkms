// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#include "gt/iov/intel_iov_provisioning.h"

static int guc2vf_payload_checker(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	struct payload_params *expected = relay->selftest.data;

	/* this must be GUC2VF handler */
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) != GUC_ACTION_GUC2VF_RELAY_FROM_PF);

	if (FIELD_GET(GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID, msg[1]) != expected->relayid)
		return -ENOTTY;

	if (len < GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN)
		return -EPROTO;

	if (len > GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN + expected->len)
		return -EMSGSIZE;

	if (len != GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN + expected->len)
		return -ENOMSG;

	if (memcmp(msg + GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN, expected->data,
		   sizeof(u32) * expected->len))
		return -EBADMSG;

	return 0;
}

static int guc2pf_payload_checker(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	struct payload_params *expected = relay->selftest.data;

	/* this must be GUC2PF handler */
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) != GUC_ACTION_GUC2PF_RELAY_FROM_VF);

	if (FIELD_GET(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, msg[1]) != expected->vfid)
		return -ENOTTY;

	if (FIELD_GET(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID, msg[2]) != expected->relayid)
		return -ENOTTY;

	if (len < GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN)
		return -EPROTO;

	if (len > GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN + expected->len)
		return -EMSGSIZE;

	if (len != GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN + expected->len)
		return -ENOMSG;

	if (memcmp(msg + GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN, expected->data,
		   sizeof(u32) * expected->len))
		return -EBADMSG;

	return 0;
}

static int pf_guc_loopback_to_vf(struct intel_iov *iov, bool fast, u32 len_min, u32 len_max)
{
	struct intel_guc_ct *ct = &iov_to_guc(iov)->ct;
	u32 request[PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN] = {
		MSG_PF2GUC_RELAY_TO_VF(0), /* loopback */
		/* ... */
	};
	struct payload_params params;
	u32 n, len;
	int ret = 0;

	GEM_BUG_ON(len_min > len_max);
	GEM_BUG_ON(len_max > PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA);

	/* fill relay data with some pattern */
	for (n = 0; n < len_max; n++)
		request[PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + n] =
			FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_n_RELAY_DATAx,
				   SELFTEST_RELAY_DATA + n);

	for (n = len_min; n <= len_max; n++) {

		params.relayid = SELFTEST_RELAY_ID + n;
		params.data = request + PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN;
		params.len = n;

		WRITE_ONCE(iov->relay.selftest.data, &params);
		WRITE_ONCE(iov->relay.selftest.guc2vf, guc2vf_payload_checker);

		request[2] = FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, params.relayid);
		len = PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + n;

		ret = intel_guc_ct_send(ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST |
					(fast ? INTEL_GUC_CT_SEND_NB : 0));
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "failed to send (nb=%s) payload len=%u, %d\n",
					   str_yes_no(fast), n, ret);
			break;
		}

		ret = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.guc2vf)), 200);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "didn't receive message len=%u, %d\n", n, ret);
			break;
		}

		ret = PTR_ERR_OR_ZERO(iov->relay.selftest.guc2vf);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "received invalid message len=%u, %d\n", n, ret);
			break;
		}
	}

	WRITE_ONCE(iov->relay.selftest.guc2vf, NULL);
	WRITE_ONCE(iov->relay.selftest.data, NULL);

	return ret;
}

static int pf_guc_loopback_min_msg_to_vf(void *arg)
{
	return pf_guc_loopback_to_vf(arg, false, 0, 0);
}

static int pf_guc_loopback_hxg_msg_to_vf(void *arg)
{
	return pf_guc_loopback_to_vf(arg, false, GUC_HXG_MSG_MIN_LEN, GUC_HXG_MSG_MIN_LEN);
}

static int pf_guc_loopback_any_msg_to_vf(void *arg)
{
	return pf_guc_loopback_to_vf(arg, false, 0, PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA);
}

static int pf_guc_loopback_any_msg_to_vf_nb(void *arg)
{
	return pf_guc_loopback_to_vf(arg, true, 0, PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA);
}

static int pf_guc_rejects_incomplete_to_vf(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc_ct *ct = &iov_to_guc(iov)->ct;
	u32 request[PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN] = {
		MSG_PF2GUC_RELAY_TO_VF(1),
	};
	unsigned int len;
	int ret = 0;

	for (len = GUC_HXG_REQUEST_MSG_MIN_LEN; len < ARRAY_SIZE(request); len++) {
		ret = intel_guc_ct_send(ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST);
		if (ret != -EIO) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't reject incomplete HXG len=%u, %d\n",
					   len, ret);
			ret = -EPROTO;
			break;
		}
		ret = 0;
	}

	return ret;
}

static int pf_guc_rejects_invalid_to_vf(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc_ct *ct = &iov_to_guc(iov)->ct;
	u32 invalid_vfid = i915_sriov_pf_get_device_totalvfs(iov_to_i915(iov)) + 1;
	u32 request[] = {
		MSG_PF2GUC_RELAY_TO_VF(invalid_vfid),
	};
	unsigned int len;
	int ret = 0;

	for (len = GUC_HXG_REQUEST_MSG_MIN_LEN; len <= ARRAY_SIZE(request); len++) {
		ret = intel_guc_ct_send(ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST);
		if (ret != -EIO) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't reject invalid VF%u len=%u, %d\n",
					   invalid_vfid, len, ret);
			ret = -EPROTO;
			break;
		}
		ret = 0;
	}

	return ret;
}

static int pf_guc_loopback_to_pf(struct intel_iov *iov, bool fast, u32 len_min, u32 len_max)
{
	struct intel_guc_ct *ct = &iov_to_guc(iov)->ct;
	u32 request[VF2GUC_RELAY_TO_PF_REQUEST_MSG_MAX_LEN] = {
		MSG_VF2GUC_RELAY_TO_PF,
		/* ... */
	};
	struct payload_params params;
	u32 n, len;
	int ret = 0;

	GEM_BUG_ON(len_min > len_max);
	GEM_BUG_ON(len_max > VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA);

	/* fill relay data with some pattern */
	for (n = 0; n < len_max; n++)
		request[VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN + n] =
			FIELD_PREP(VF2GUC_RELAY_TO_PF_REQUEST_MSG_n_RELAY_DATAx,
				   SELFTEST_RELAY_DATA - n);

	for (n = len_min; n <= len_max; n++) {

		params.vfid = PFID;
		params.relayid = SELFTEST_RELAY_ID + n;
		params.len = n;
		params.data = request + VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN;

		WRITE_ONCE(iov->relay.selftest.data, &params);
		WRITE_ONCE(iov->relay.selftest.guc2pf, guc2pf_payload_checker);

		request[1] = FIELD_PREP(VF2GUC_RELAY_TO_PF_REQUEST_MSG_1_RELAY_ID, params.relayid);
		len = VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN + n;

		ret = intel_guc_ct_send(ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST |
					(fast ? INTEL_GUC_CT_SEND_NB : 0));
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "failed to send (nb=%s) payload len=%u, %d\n",
					   str_yes_no(fast), n, ret);
			break;
		}

		ret = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.guc2pf)), 200);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "didn't receive message len=%u, %d\n", n, ret);
			break;
		}

		ret = PTR_ERR_OR_ZERO(iov->relay.selftest.guc2pf);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "received invalid message len=%u, %d\n", n, ret);
			break;
		}
	}

	WRITE_ONCE(iov->relay.selftest.guc2pf, NULL);
	WRITE_ONCE(iov->relay.selftest.data, NULL);

	return ret;
}

static int pf_guc_loopback_min_msg_to_pf(void *arg)
{
	return pf_guc_loopback_to_pf(arg, false, 0, 0);
}

static int pf_guc_loopback_hxg_msg_to_pf(void *arg)
{
	return pf_guc_loopback_to_pf(arg, false, GUC_HXG_MSG_MIN_LEN, GUC_HXG_MSG_MIN_LEN);
}

static int pf_guc_loopback_any_msg_to_pf(void *arg)
{
	return pf_guc_loopback_to_pf(arg, false, 0, VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA);
}

static int pf_guc_loopback_any_msg_to_pf_nb(void *arg)
{
	return pf_guc_loopback_to_pf(arg, true, 0, VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA);
}

static int pf_guc_rejects_incomplete_to_pf(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc_ct *ct = &iov_to_guc(iov)->ct;
	u32 request[VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN] = {
		MSG_VF2GUC_RELAY_TO_PF,
	};
	unsigned int len;
	int ret = 0;

	for (len = GUC_HXG_REQUEST_MSG_MIN_LEN; len < ARRAY_SIZE(request); len++) {
		ret = intel_guc_ct_send(ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST);
		if (ret != -EIO) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't reject incomplete HXG len=%u, %d\n",
					   len, ret);
			ret = -EPROTO;
			break;
		}
		ret = 0;
	}

	return ret;
}

static int pf_loopback_one_way_to_vf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA] = {
		MSG_IOV_SELFTEST_RELAY_EVENT(SELFTEST_RELAY_OPCODE_NOP),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		/* ... */
	};
	struct payload_params params;
	unsigned int n;
	int ret = 0;

	iov->relay.selftest.enable_loopback = 1;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		params.relayid = SELFTEST_RELAY_ID + n;
		params.vfid = PFID; /* loopback */
		params.data = msg;
		params.len = n;

		WRITE_ONCE(iov->relay.selftest.data, &params);
		WRITE_ONCE(iov->relay.selftest.guc2vf, guc2vf_payload_checker);

		/* can't use intel_iov_relay_send_to_vf() as we need our relayid */
		ret = relay_send(&iov->relay, params.vfid, params.relayid, msg, n);

		if (ret) {
			IOV_SELFTEST_ERROR(iov, "len=%u, %d\n", n, ret);
			break;
		}

		ret = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.guc2vf)), 200);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "message not received len=%u, %d\n", n, ret);
			break;
		}

		ret = PTR_ERR_OR_ZERO(iov->relay.selftest.guc2vf);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "corrupted message len=%u, %d\n", n, ret);
			break;
		}
	}

	iov->relay.selftest.enable_loopback = 0;
	WRITE_ONCE(iov->relay.selftest.guc2vf, NULL);
	WRITE_ONCE(iov->relay.selftest.data, NULL);

	return ret;
}

static int pf_full_loopback_to_vf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA] = {
		MSG_IOV_SELFTEST_RELAY(SELFTEST_RELAY_OPCODE_NOP),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		/* ... */
	};
	u32 buf[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA];
	unsigned int n;
	int ret = 0;

	iov->relay.selftest.enable_loopback = 1;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		ret = intel_iov_relay_send_to_vf(&iov->relay, PFID, msg, n,
						 buf, ARRAY_SIZE(buf));

		if (ret < 0) {
			IOV_SELFTEST_ERROR(iov, "failed to send msg len=%u, %d\n", n, ret);
			break;
		}

		if (ret != GUC_HXG_MSG_MIN_LEN) {
			IOV_SELFTEST_ERROR(iov, "unexpected nop reply len=%u, %d\n", n, ret);
			ret = -ENODATA;
			break;
		}

		ret = 0;
	}

	iov->relay.selftest.enable_loopback = 0;

	return ret;
}

static int pf_loopback_one_way_to_pf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg[VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA] = {
		MSG_IOV_SELFTEST_RELAY_EVENT(SELFTEST_RELAY_OPCODE_NOP),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		/* ... */
	};
	struct payload_params params;
	unsigned int n;
	int ret = 0;

	iov->relay.selftest.disable_strict = 1;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		params.vfid = PFID;
		params.relayid = SELFTEST_RELAY_ID + n;
		params.data = msg;
		params.len = n;

		WRITE_ONCE(iov->relay.selftest.data, &params);
		WRITE_ONCE(iov->relay.selftest.guc2pf, guc2pf_payload_checker);

		/* can't use intel_iov_relay_send_to_pf() as we need our relayid */
		ret = relay_send(&iov->relay, 0, params.relayid, msg, n);

		if (ret) {
			IOV_SELFTEST_ERROR(iov, "len=%u, %d\n", n, ret);
			break;
		}

		ret = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.guc2pf)), 200);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "message not received len=%u, %d\n", n, ret);
			break;
		}

		ret = PTR_ERR_OR_ZERO(iov->relay.selftest.guc2pf);
		if (ret) {
			IOV_SELFTEST_ERROR(iov, "corrupted message len=%u, %d\n", n, ret);
			break;
		}
	}

	iov->relay.selftest.disable_strict = 0;
	WRITE_ONCE(iov->relay.selftest.guc2pf, NULL);
	WRITE_ONCE(iov->relay.selftest.data, NULL);

	return ret;
}

static int relay_request_to_pf(struct intel_iov *iov)
{
	u32 msg[VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA] = {
		MSG_IOV_SELFTEST_RELAY(SELFTEST_RELAY_OPCODE_NOP),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		/* ... */
	};
	u32 buf[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA];
	unsigned int n;
	int ret = 0;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		ret = intel_iov_relay_send_to_pf(&iov->relay, msg, n,
						 buf, ARRAY_SIZE(buf));

		if (ret < 0) {
			IOV_SELFTEST_ERROR(iov, "failed to send len=%u, %d\n", n, ret);
			break;
		}

		if (ret != GUC_HXG_MSG_MIN_LEN) {
			IOV_SELFTEST_ERROR(iov, "unexpected nop reply len=%u, %d\n", n, ret);
			ret = -ENODATA;
			break;
		}

		ret = 0;
	}

	return ret;
}

static int pf_full_loopback_to_pf(void *arg)
{
	struct intel_iov *iov = arg;
	int err;

	iov->relay.selftest.disable_strict = 1;
	iov->relay.selftest.enable_loopback = 1;

	err = relay_request_to_pf(iov);

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;

	return err;
}

static int vf_send_request_to_pf(void *arg)
{
	return relay_request_to_pf(arg);
}

int selftest_live_iov_relay(struct drm_i915_private *i915)
{
	static const struct i915_subtest pf_tests[] = {
		SUBTEST(pf_guc_loopback_min_msg_to_vf),
		SUBTEST(pf_guc_loopback_hxg_msg_to_vf),
		SUBTEST(pf_guc_loopback_any_msg_to_vf),
		SUBTEST(pf_guc_loopback_any_msg_to_vf_nb),
		SUBTEST(pf_guc_rejects_incomplete_to_vf),
		SUBTEST(pf_guc_rejects_invalid_to_vf),
		SUBTEST(pf_guc_loopback_min_msg_to_pf),
		SUBTEST(pf_guc_loopback_hxg_msg_to_pf),
		SUBTEST(pf_guc_loopback_any_msg_to_pf),
		SUBTEST(pf_guc_loopback_any_msg_to_pf_nb),
		SUBTEST(pf_guc_rejects_incomplete_to_pf),
		SUBTEST(pf_loopback_one_way_to_vf),
		SUBTEST(pf_full_loopback_to_vf),
		SUBTEST(pf_loopback_one_way_to_pf),
		SUBTEST(pf_full_loopback_to_pf),
	};
	static const struct i915_subtest vf_tests[] = {
		SUBTEST(vf_send_request_to_pf),
	};
	intel_wakeref_t wakeref;
	int err = 0;

	if (!IS_SRIOV(i915))
		return 0;

	if (IS_SRIOV_PF(i915) && i915_sriov_pf_status(i915) < 0)
		return -EHOSTDOWN;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		struct intel_iov *iov = &to_gt(i915)->iov;

		if (IS_SRIOV_PF(i915)) {
			err = intel_iov_provisioning_force_vgt_mode(iov);
			if (err)
				break;
			err = intel_iov_live_subtests(pf_tests, iov);
		} else if (IS_SRIOV_VF(i915)) {
			err = intel_iov_live_subtests(vf_tests, iov);
		}
		if (err)
			break;
	}

	return err;
}
