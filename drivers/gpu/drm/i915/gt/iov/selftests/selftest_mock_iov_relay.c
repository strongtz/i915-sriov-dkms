// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#include "selftests/mock_gem_device.h"

static int host2guc_success(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	IOV_DEBUG(relay_to_iov(relay), "attempt to send [%u] %*ph\n", len, 4 * len, msg);
	return 0;
}

static int mock_accepts_min_msg(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_guc2pf[GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
	};
	u32 msg_guc2vf[GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN] = {
		MSG_GUC2VF_RELAY_FROM_PF,
	};
	int err, ret = 0;

	iov->relay.selftest.disable_strict = 1;
	iov->relay.selftest.host2guc = host2guc_success;

	do {
		err = intel_iov_relay_process_guc2pf(&iov->relay, msg_guc2pf, ARRAY_SIZE(msg_guc2pf));
		if (err) {
			IOV_SELFTEST_ERROR(iov, "GUC2PF was rejected %d (%pe)\n", err, ERR_PTR(err));
			ret = -ENOTSOCK;
			break;
		}

		err = intel_iov_relay_process_guc2vf(&iov->relay, msg_guc2vf, ARRAY_SIZE(msg_guc2vf));
		if (err) {
			IOV_SELFTEST_ERROR(iov, "GUC2VF was rejected %d (%pe)\n", err, ERR_PTR(err));
			ret = -ENOTSOCK;
			break;
		}
	} while (0);

	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;

	return ret;
}

static int mock_drops_msg_if_native(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_guc2pf[GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
	};
	u32 msg_guc2vf[GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN] = {
		MSG_GUC2VF_RELAY_FROM_PF,
	};
	int err, ret = 0;

	iov->relay.selftest.host2guc = host2guc_success;

	do {
		err = intel_iov_relay_process_guc2pf(&iov->relay, msg_guc2pf, ARRAY_SIZE(msg_guc2pf));
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", "guc2pf", err, ERR_PTR(err));
		if (!err) {
			IOV_SELFTEST_ERROR(iov, "GUC2PF was not rejected\n");
			ret = -ENOTSOCK;
			break;
		}

		err = intel_iov_relay_process_guc2vf(&iov->relay, msg_guc2vf, ARRAY_SIZE(msg_guc2vf));
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", "guc2vf", err, ERR_PTR(err));
		if (!err) {
			IOV_SELFTEST_ERROR(iov, "GUC2VF was not rejected\n");
			ret = -ENOTSOCK;
			break;
		}
	} while (0);

	iov->relay.selftest.host2guc = NULL;

	return ret;
}

static int mock_drops_malformed_guc2pf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_no_vfid[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2PF_RELAY_FROM_VF),
	};
	u32 msg_no_relayid[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2PF_RELAY_FROM_VF),
		FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, VFID(1)),
	};
	u32 msg_unexpected_subaction[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_DATA0, /* unexpected */ 1) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2PF_RELAY_FROM_VF),
		FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, VFID(1)),
		FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID, SELFTEST_RELAY_ID),
	};
	u32 msg_unexpected_vfid[GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN] = {
		MSG_GUC2PF_RELAY_FROM_VF(0),
	};
	u32 msg_too_long[GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN + 1] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
	};
	struct {
		const char *name;
		const u32 *msg;
		u32 len;
	} testcases[] = {
#define TC(X) { #X, X, ARRAY_SIZE(X) }
		TC(msg_no_vfid),
		TC(msg_no_relayid),
		TC(msg_unexpected_subaction),
		TC(msg_unexpected_vfid),
		TC(msg_too_long),
#undef TC
		{ }
	}, *tc = testcases;
	int err, ret = 0;

	iov->relay.selftest.disable_strict = 1;
	iov->relay.selftest.host2guc = host2guc_success;

	while (tc->name) {
		err = intel_iov_relay_process_guc2pf(&iov->relay, tc->msg, tc->len);
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", tc->name, err, ERR_PTR(err));
		if (!err) {
			IOV_SELFTEST_ERROR(iov, "%s was not rejected\n", tc->name);
			ret = -ENOTSOCK;
			break;
		}
		tc++;
	}

	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;

	return ret;
}

static int mock_drops_malformed_guc2vf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_no_relayid[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2VF_RELAY_FROM_PF),
	};
	u32 msg_unexpected_subaction[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_DATA0, /* unexpected */ 1) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, GUC_ACTION_GUC2VF_RELAY_FROM_PF),
		FIELD_PREP(GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID, SELFTEST_RELAY_ID),
	};
	u32 msg_too_long[GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN + 1] = {
		MSG_GUC2VF_RELAY_FROM_PF,
	};
	struct {
		const char *name;
		const u32 *msg;
		u32 len;
	} testcases[] = {
#define TC(X) { #X, X, ARRAY_SIZE(X) }
		TC(msg_no_relayid),
		TC(msg_unexpected_subaction),
		TC(msg_too_long),
#undef TC
		{ }
	}, *tc = testcases;
	int err, ret = 0;

	iov->relay.selftest.disable_strict = 1;
	iov->relay.selftest.host2guc = host2guc_success;

	while (tc->name) {
		err = intel_iov_relay_process_guc2vf(&iov->relay, tc->msg, tc->len);
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", tc->name, err, ERR_PTR(err));
		if (!err) {
			IOV_SELFTEST_ERROR(iov, "%s was not rejected\n", tc->name);
			ret = -ENOTSOCK;
			break;
		}
		tc++;
	}

	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;

	return ret;
}

static int mock_ignores_unexpected_guc2pf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_bad_origin[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY),
	};
	u32 msg_success[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS),
	};
	u32 msg_failure[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_FAILURE),
	};
	u32 msg_retry[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_RETRY),
	};
	u32 msg_busy[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_BUSY),
	};
	u32 msg_reserved2[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, 2),
	};
	u32 msg_reserved4[] = {
		MSG_GUC2PF_RELAY_FROM_VF(1),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, 4),
	};
	struct {
		const char *name;
		const u32 *msg;
		u32 len;
	} testcases[] = {
#define TC(X) { #X, X, ARRAY_SIZE(X) }
		TC(msg_bad_origin),
		TC(msg_success),
		TC(msg_failure),
		TC(msg_retry),
		TC(msg_busy),
		TC(msg_reserved2),
		TC(msg_reserved4),
#undef TC
		{ }
	}, *tc = testcases;
	int err, ret = 0;

	iov->relay.selftest.disable_strict = 1;

	while (tc->name) {
		err = intel_iov_relay_process_guc2pf(&iov->relay, tc->msg, tc->len);
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", tc->name, err, ERR_PTR(err));
		if (!err) {
			IOV_SELFTEST_ERROR(iov, "%s was not rejected\n", tc->name);
			ret = -ENOTSOCK;
			break;
		}
		tc++;
	}

	iov->relay.selftest.disable_strict = 0;

	return ret;
}

static int mock_ignores_unexpected_guc2vf(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_bad_origin[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY),
	};
	u32 msg_success[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS),
	};
	u32 msg_failure[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_FAILURE),
	};
	u32 msg_retry[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_RETRY),
	};
	u32 msg_busy[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_BUSY),
	};
	u32 msg_reserved2[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, 2),
	};
	u32 msg_reserved4[] = {
		MSG_GUC2VF_RELAY_FROM_PF,
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, 4),
	};
	struct {
		const char *name;
		const u32 *msg;
		u32 len;
	} testcases[] = {
#define TC(X) { #X, X, ARRAY_SIZE(X) }
		TC(msg_bad_origin),
		TC(msg_success),
		TC(msg_failure),
		TC(msg_retry),
		TC(msg_busy),
		TC(msg_reserved2),
		TC(msg_reserved4),
#undef TC
		{ }
	}, *tc = testcases;
	int err, ret = 0;

	iov->relay.selftest.disable_strict = 1;

	while (tc->name) {
		err = intel_iov_relay_process_guc2vf(&iov->relay, tc->msg, tc->len);
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", tc->name, err, ERR_PTR(err));
		if (!err) {
			IOV_SELFTEST_ERROR(iov, "%s was not rejected\n", tc->name);
			ret = -ENOTSOCK;
			break;
		}
		tc++;
	}

	iov->relay.selftest.disable_strict = 0;

	return ret;
}

struct payload_params {
	u32 vfid;
	u32 relayid;
	const u32 *data;
	u32 len;
};

static int pf2guc_payload_checker(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	struct payload_params *expected = relay->selftest.data;

	host2guc_success(relay, msg, len);

	if (len < PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN)
		return -EPROTO;

	if (len > PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN)
		return -EMSGSIZE;

	if (FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_HOST)
		return -EPROTO;

	/* XXX use FAST REQUEST */
	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_REQUEST)
		return -EPROTO;

	if (FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]) != GUC_ACTION_PF2GUC_RELAY_TO_VF)
		return -ENOTTY;

	if (FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, msg[1]) != expected->vfid)
		return -ENOTTY;

	if (FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, msg[2]) != expected->relayid)
		if (expected->relayid)
			return -ENOTTY;

	if (expected->len > PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA)
		return -EINVAL;

	if (len > PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + expected->len)
		return -EMSGSIZE;

	if (len != PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + expected->len)
		return -ENOMSG;

	if (memcmp(msg + PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN, expected->data,
		   sizeof(u32) * expected->len))
		return -EBADMSG;

	return 0;
}

static int mock_prepares_pf2guc(void *arg)
{
	struct intel_iov *iov = arg;
	u32 vfid = VFID(1);
	u32 msg[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, ~SELFTEST_RELAY_DATA),
	};
	u32 buf[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA];
	struct payload_params params;
	unsigned int n;
	int err = 0;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		params.relayid = 0; /* don't check */
		params.vfid = vfid;
		params.data = msg;
		params.len = n;

		iov->relay.selftest.disable_strict = 1;
		iov->relay.selftest.enable_loopback = 1;
		iov->relay.selftest.data = &params;
		iov->relay.selftest.host2guc = pf2guc_payload_checker;

		err = intel_iov_relay_send_to_vf(&iov->relay, vfid, msg, n,
						 buf, ARRAY_SIZE(buf));

		if (err < 0) {
			IOV_SELFTEST_ERROR(iov, "failed to send msg len=%u, %d\n", n, err);
			break;
		}

		err = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.host2guc)), 200);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "didn't send msg len=%u, %d\n", n, err);
			break;
		}

		err = PTR_ERR_OR_ZERO(iov->relay.selftest.host2guc);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "invalid msg len=%u, %d\n", n, err);
			break;
		}
	}

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;
	iov->relay.selftest.data = NULL;

	return err;
}

static int pf2guc_auto_reply_success(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	u32 reply[] = {
		MSG_GUC2PF_RELAY_FROM_VF(0),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS),
	};
	int err = pf2guc_payload_checker(relay, msg, len);

	if (err)
		return err;

	reply[1] = FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID,
			      FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, msg[1]));
	reply[2] = FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID,
			      FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, msg[2]));

	intel_iov_relay_process_guc2pf(relay, reply, ARRAY_SIZE(reply));
	return 0;
}

static int mock_prepares_pf2guc_and_waits(void *arg)
{
	struct intel_iov *iov = arg;
	u32 vfid = VFID(1);
	u32 msg[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
	};
	u32 buf[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA];
	struct payload_params params;
	unsigned int n;
	int err = 0;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		params.relayid = 0; /* don't check */
		params.vfid = vfid;
		params.data = msg;
		params.len = n;

		iov->relay.selftest.disable_strict = 1;
		iov->relay.selftest.enable_loopback = 1;
		iov->relay.selftest.data = &params;
		iov->relay.selftest.host2guc = pf2guc_auto_reply_success;

		err = intel_iov_relay_send_to_vf(&iov->relay, vfid, msg, n,
						 buf, ARRAY_SIZE(buf));

		if (err < 0) {
			IOV_SELFTEST_ERROR(iov, "failed to send msg len=%u, %d\n", n, err);
			break;
		}

		err = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.host2guc)), 200);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "didn't send msg len=%u, %d\n", n, err);
			break;
		}

		err = PTR_ERR_OR_ZERO(iov->relay.selftest.host2guc);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "invalid msg len=%u, %d\n", n, err);
			break;
		}
	}

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;
	iov->relay.selftest.data = NULL;

	return err;
}

static int pf2guc_auto_reply_failure(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	u32 reply[] = {
		MSG_GUC2PF_RELAY_FROM_VF(0),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_FAILURE) |
		FIELD_PREP(GUC_HXG_FAILURE_MSG_0_ERROR, IOV_ERROR_NO_DATA_AVAILABLE),
	};
	int err = pf2guc_payload_checker(relay, msg, len);

	if (err)
		return err;

	reply[1] = FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID,
			      FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, msg[1]));
	reply[2] = FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID,
			      FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, msg[2]));

	intel_iov_relay_process_guc2pf(relay, reply, ARRAY_SIZE(reply));
	return 0;
}

static int mock_prepares_pf2guc_and_fails(void *arg)
{
	struct intel_iov *iov = arg;
	u32 vfid = VFID(1);
	u32 msg[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
	};
	u32 buf[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA];
	struct payload_params params;
	unsigned int n;
	int err = 0;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		params.relayid = 0; /* don't check */
		params.vfid = vfid;
		params.data = msg;
		params.len = n;

		iov->relay.selftest.disable_strict = 1;
		iov->relay.selftest.enable_loopback = 1;
		iov->relay.selftest.data = &params;
		iov->relay.selftest.host2guc = pf2guc_auto_reply_failure;

		err = intel_iov_relay_send_to_vf(&iov->relay, vfid, msg, n,
						 buf, ARRAY_SIZE(buf));

		if (err > 0) {
			IOV_SELFTEST_ERROR(iov, "unexpected success msg len=%u, %d\n", n, err);
			break;
		}

		err = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.host2guc)), 200);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "didn't send msg len=%u, %d\n", n, err);
			break;
		}

		err = PTR_ERR_OR_ZERO(iov->relay.selftest.host2guc);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "invalid msg len=%u, %d\n", n, err);
			break;
		}
	}

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;
	iov->relay.selftest.data = NULL;

	return err;
}

static int pf2guc_auto_reply_retry(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	u32 reply[] = {
		MSG_GUC2PF_RELAY_FROM_VF(0),
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_NO_RESPONSE_RETRY) |
		FIELD_PREP(GUC_HXG_RETRY_MSG_0_REASON, GUC_HXG_RETRY_REASON_UNSPECIFIED),
	};
	int err = pf2guc_payload_checker(relay, msg, len);

	if (err)
		return err;

	reply[1] = FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID,
			      FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, msg[1]));
	reply[2] = FIELD_PREP(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID,
			      FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, msg[2]));

	intel_iov_relay_process_guc2pf(relay, reply, ARRAY_SIZE(reply));
	return 0;
}

static int mock_prepares_pf2guc_and_retries(void *arg)
{
	struct intel_iov *iov = arg;
	u32 vfid = VFID(1);
	u32 msg[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_SELFTEST_RELAY),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
	};
	u32 buf[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA];
	struct payload_params params;
	unsigned int n;
	int err = 0;

	return 0;

	for (n = GUC_HXG_MSG_MIN_LEN; n <= ARRAY_SIZE(msg); n++) {

		params.relayid = 0; /* don't check */
		params.vfid = vfid;
		params.data = msg;
		params.len = n;

		iov->relay.selftest.disable_strict = 1;
		iov->relay.selftest.enable_loopback = 1;
		iov->relay.selftest.data = &params;
		iov->relay.selftest.host2guc = pf2guc_auto_reply_retry;

		err = intel_iov_relay_send_to_vf(&iov->relay, vfid, msg, n,
						 buf, ARRAY_SIZE(buf));

		if (err > 0) {
			IOV_SELFTEST_ERROR(iov, "unexpected success msg len=%u, %d\n", n, err);
			break;
		}

		err = wait_for(IS_ERR_OR_NULL(READ_ONCE(iov->relay.selftest.host2guc)), 200);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "didn't send msg len=%u, %d\n", n, err);
			break;
		}

		err = PTR_ERR_OR_ZERO(iov->relay.selftest.host2guc);
		if (err) {
			IOV_SELFTEST_ERROR(iov, "invalid msg len=%u, %d\n", n, err);
			break;
		}
	}

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;
	iov->relay.selftest.host2guc = NULL;
	iov->relay.selftest.data = NULL;

	return err;
}

int selftest_mock_iov_relay(void)
{
	static const struct i915_subtest mock_tests[] = {
		SUBTEST(mock_accepts_min_msg),
		SUBTEST(mock_drops_msg_if_native),
		SUBTEST(mock_drops_malformed_guc2pf),
		SUBTEST(mock_drops_malformed_guc2vf),
		SUBTEST(mock_ignores_unexpected_guc2pf),
		SUBTEST(mock_ignores_unexpected_guc2vf),
		SUBTEST(mock_prepares_pf2guc),
		SUBTEST(mock_prepares_pf2guc_and_waits),
		SUBTEST(mock_prepares_pf2guc_and_fails),
		SUBTEST(mock_prepares_pf2guc_and_retries),
	};
	struct drm_i915_private *i915;
	struct intel_iov *iov;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	iov = &to_gt(i915)->iov;
	intel_iov_relay_init_early(&iov->relay);

	err = i915_subtests(mock_tests, iov);

	mock_destroy_device(i915);
	return err;
}
