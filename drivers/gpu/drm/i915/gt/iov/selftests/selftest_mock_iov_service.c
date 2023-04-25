// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#include "gt/intel_gt.h"
#include "gt/iov/abi/iov_actions_abi.h"
#include "gt/iov/abi/iov_version_abi.h"
#include "selftests/mock_gem_device.h"

#define SELFTEST_RELAY_ID       0x76543210
#define SELFTEST_VF_ID		1

static int mock_drop_malformed_handshake_msg(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg_invalid_mbz[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_0_MBZ, 1) | /* non zero MBZ */
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_HANDSHAKE),
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, IOV_VERSION_LATEST_MAJOR) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, IOV_VERSION_LATEST_MINOR)
	};
	u32 msg_invalid_version[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_HANDSHAKE),
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, 0) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, 1)
	};
	u32 msg_too_short[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_HANDSHAKE),
	};
	u32 msg_too_long[VF2PF_HANDSHAKE_REQUEST_MSG_LEN + 1] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_HANDSHAKE),
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, IOV_VERSION_LATEST_MAJOR) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, IOV_VERSION_LATEST_MINOR),
	};
	struct {
		const char *name;
		const u32 *msg;
		u32 len;
	} testcases[] = {
#define TC(X) { #X, X, ARRAY_SIZE(X) }
		TC(msg_invalid_mbz),
		TC(msg_invalid_version),
		TC(msg_too_short),
		TC(msg_too_long),
#undef TC
		{ }
	}, *tc = testcases;
	int err, ret = 0;

	while (tc->name) {
		err = intel_iov_service_process_msg(iov, SELFTEST_VF_ID, SELFTEST_RELAY_ID, tc->msg,
						    tc->len);
		IOV_DEBUG(iov, "processing %s returned %d (%pe)\n", tc->name, err, ERR_PTR(err));

		if (!err) {
			IOV_SELFTEST_ERROR(iov, "%s was not rejected\n", tc->name);
			ret = -ENOTSOCK;
			break;
		}
		tc++;
	}

	return ret;
}

static int host2guc_success(struct intel_iov_relay *relay, const u32 *msg_recvd, u32 len)
{
	u32 *expected_response = relay->selftest.data;
	const u32 *relay_msg;

	GEM_BUG_ON(len < PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN);
	GEM_BUG_ON(len > PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg_recvd[0]) != GUC_HXG_TYPE_REQUEST);
	GEM_BUG_ON(FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, msg_recvd[1]) !=
			     SELFTEST_VF_ID);
	GEM_BUG_ON(FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, msg_recvd[2]) !=
			     SELFTEST_RELAY_ID);

	/* msg_recvd is full H2G, extract IOV message */
	relay_msg = msg_recvd + PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN;
	if (!memcmp(expected_response, relay_msg, sizeof(*expected_response)))
		return 0;

	return -ENOTTY;
}

static int mock_try_handshake(struct intel_iov *iov, u32 major_wanted, u32 minor_wanted,
			      u32 major, u32 minor)
{
	u32 msg[VF2PF_HANDSHAKE_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_HANDSHAKE),
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, major_wanted) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, minor_wanted),
	};
	u32 response[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		FIELD_PREP(GUC_HXG_RESPONSE_MSG_0_DATA0, 0),
		FIELD_PREP(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR, major) |
		FIELD_PREP(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR, minor),
	};
	int ret = 0;

	iov->relay.selftest.data = &response;
	iov->relay.selftest.host2guc = host2guc_success;

	ret = intel_iov_service_process_msg(iov, SELFTEST_VF_ID, SELFTEST_RELAY_ID, msg,
					    ARRAY_SIZE(msg));

	iov->relay.selftest.host2guc = NULL;
	iov->relay.selftest.data = NULL;

	return ret;
}

static int mock_handshake_baseline(void *arg)
{
	struct intel_iov *iov = arg;
	int err, ret = 0;

	err = mock_try_handshake(iov, 1, 0, 1, 0);

	if (err) {
		IOV_SELFTEST_ERROR(iov, "Service message rejected %d (%pe)\n", err, ERR_PTR(err));
		ret = -ENOTSOCK;
	}

	return ret;
}

static int mock_handshake_full_match(void *arg)
{
	struct intel_iov *iov = arg;
	int err, ret = 0;

	err = mock_try_handshake(iov, IOV_VERSION_LATEST_MAJOR, IOV_VERSION_LATEST_MINOR,
				 IOV_VERSION_LATEST_MAJOR, IOV_VERSION_LATEST_MINOR);

	if (err) {
		IOV_SELFTEST_ERROR(iov, "Service message rejected %d (%pe)\n", err, ERR_PTR(err));
		ret = -ENOTSOCK;
	}

	return ret;
}

static int mock_handshake_with_newer(void *arg)
{
	struct intel_iov *iov = arg;
	int err, ret = 0;

	err = mock_try_handshake(iov, IOV_VERSION_LATEST_MAJOR, IOV_VERSION_LATEST_MINOR + 1,
				 IOV_VERSION_LATEST_MAJOR, IOV_VERSION_LATEST_MINOR);

	if (err) {
		IOV_SELFTEST_ERROR(iov, "Service message rejected %d (%pe)\n", err, ERR_PTR(err));
		ret = -ENOTSOCK;
	}

	return ret;
}

static int mock_handshake_latest_pf_support(void *arg)
{
	struct intel_iov *iov = arg;
	int err, ret = 0;

	err = mock_try_handshake(iov, 0, 0, IOV_VERSION_LATEST_MAJOR, IOV_VERSION_LATEST_MINOR);

	if (err) {
		IOV_SELFTEST_ERROR(iov, "Service message rejected %d (%pe)\n", err, ERR_PTR(err));
		ret = -ENOTSOCK;
	}

	return ret;
}

static int mock_handshake_reject_invalid(void *arg)
{
	struct intel_iov *iov = arg;
	int err, ret = 0;

	err = mock_try_handshake(iov, 0, 1, IOV_VERSION_LATEST_MAJOR, IOV_VERSION_LATEST_MINOR);

	if (err != -EINVAL) {
		IOV_SELFTEST_ERROR(iov, "Service message rejected %d (%pe)\n", err, ERR_PTR(err));
		ret = -ENOTSOCK;
	}

	return ret;
}

int selftest_mock_iov_service(void)
{
	static const struct i915_subtest mock_tests[] = {
		SUBTEST(mock_drop_malformed_handshake_msg),
		SUBTEST(mock_handshake_baseline),
		SUBTEST(mock_handshake_full_match),
		SUBTEST(mock_handshake_with_newer),
		SUBTEST(mock_handshake_latest_pf_support),
		SUBTEST(mock_handshake_reject_invalid),
	};
	struct drm_i915_private *i915;
	struct intel_iov *iov;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	i915->__mode = I915_IOV_MODE_SRIOV_PF;
	iov = &to_gt(i915)->iov;
	intel_iov_relay_init_early(&iov->relay);

	err = i915_subtests(mock_tests, iov);

	i915->__mode = I915_IOV_MODE_NONE;
	mock_destroy_device(i915);

	return err;
}
