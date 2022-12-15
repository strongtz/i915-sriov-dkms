// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/bitfield.h>

#include "abi/iov_actions_abi.h"
#include "abi/iov_actions_selftest_abi.h"
#include "abi/iov_errors_abi.h"
#include "abi/iov_messages_abi.h"
#include "gt/intel_gt.h"
#include "intel_iov.h"
#include "intel_iov_relay.h"
#include "intel_iov_service.h"
#include "intel_iov_utils.h"
#include "intel_runtime_pm.h"
#include "i915_drv.h"
#include "i915_gem.h"

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
static int relay_selftest_process_msg(struct intel_iov_relay *, u32, u32, const u32 *, u32);
static int relay_selftest_guc_send_nb(struct intel_guc *, const u32 *, u32, u32);
#define intel_guc_send    relay_selftest_override_not_available
#define intel_guc_ct_send relay_selftest_override_not_available
#define intel_guc_send_nb relay_selftest_guc_send_nb
#endif

static struct intel_iov *relay_to_iov(struct intel_iov_relay *relay)
{
	return container_of(relay, struct intel_iov, relay);
}

static struct intel_gt *relay_to_gt(struct intel_iov_relay *relay)
{
	return iov_to_gt(relay_to_iov(relay));
}

static struct intel_guc *relay_to_guc(struct intel_iov_relay *relay)
{
	return &relay_to_gt(relay)->uc.guc;
}

static struct drm_i915_private *relay_to_i915(struct intel_iov_relay *relay)
{
	return relay_to_gt(relay)->i915;
}

__maybe_unused
static struct device *relay_to_dev(struct intel_iov_relay *relay)
{
	return relay_to_i915(relay)->drm.dev;
}

#define RELAY_DEBUG(_r, _f, ...) \
	IOV_DEBUG(relay_to_iov(_r), "relay: " _f, ##__VA_ARGS__)
#define RELAY_ERROR(_r, _f, ...) \
	IOV_ERROR(relay_to_iov(_r), "relay: " _f, ##__VA_ARGS__)
#define RELAY_PROBE_ERROR(_r, _f, ...) \
	IOV_PROBE_ERROR(relay_to_iov(_r), "relay: " _f, ##__VA_ARGS__)

/*
 * How long should we wait for the response?
 * For default timeout use CONFIG_DRM_I915_HEARTBEAT_INTERVAL like CTB does.
 * If hearbeat interval is not enabled then wait forever.
 */
#define RELAY_TIMEOUT	(CONFIG_DRM_I915_HEARTBEAT_INTERVAL ?: MAX_SCHEDULE_TIMEOUT)

static u32 relay_get_next_fence(struct intel_iov_relay *relay)
{
	u32 fence;

	spin_lock(&relay->lock);
	fence = ++relay->last_fence;
	if (unlikely(!fence))
		fence = relay->last_fence = 1;
	spin_unlock(&relay->lock);
	return fence;
}

struct pending_relay {
	struct list_head link;
	struct completion done;
	u32 target;
	u32 fence;
	int reply;
	u32 *response; /* can't be null */
	u32 response_size;
};

static int pf_relay_send(struct intel_iov_relay *relay, u32 target,
			 u32 relay_id, const u32 *msg, u32 len)
{
	u32 request[PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_RELAY_TO_VF),
		FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, target),
		FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, relay_id),
	};
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(!target && !I915_SELFTEST_ONLY(relay->selftest.enable_loopback));
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len + PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN >
		   PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN);

	memcpy(&request[PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN], msg, 4 * len);

retry:
	err = intel_guc_send_nb(relay_to_guc(relay), request,
				PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + len, 0);
	if (unlikely(err == -EBUSY))
		goto retry;

	return err;
}

static int vf_relay_send(struct intel_iov_relay *relay,
			 u32 relay_id, const u32 *msg, u32 len)
{
	u32 request[VF2GUC_RELAY_TO_PF_REQUEST_MSG_MAX_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_RELAY_TO_PF),
		FIELD_PREP(VF2GUC_RELAY_TO_PF_REQUEST_MSG_1_RELAY_ID, relay_id),
	};
	int err;

	GEM_BUG_ON(!IS_SRIOV_VF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len + VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN >
		   VF2GUC_RELAY_TO_PF_REQUEST_MSG_MAX_LEN);

	memcpy(&request[VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN], msg, 4 * len);

retry:
	err = intel_guc_send_nb(relay_to_guc(relay), request,
				VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN + len, 0);
	if (unlikely(err == -EBUSY))
		goto retry;

	return err;
}

static int relay_send(struct intel_iov_relay *relay, u32 target,
		      u32 relay_id, const u32 *msg, u32 len)
{
	int err;

	GEM_BUG_ON(!len);
	RELAY_DEBUG(relay, "sending %s.%u to %u = %*ph\n",
		    hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])),
		    relay_id, target, 4 * len, msg);

	if (target || I915_SELFTEST_ONLY(relay->selftest.enable_loopback))
		err = pf_relay_send(relay, target, relay_id, msg, len);
	else
		err = vf_relay_send(relay, relay_id, msg, len);

	if (unlikely(err < 0))
		RELAY_PROBE_ERROR(relay, "Failed to send %s.%u to %u (%pe) %*ph\n",
				  hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])),
				  relay_id, target, ERR_PTR(err), 4 * len, msg);
	return err;
}

/**
 * intel_iov_relay_reply_to_vf - Send reply message to VF.
 * @relay: the Relay struct
 * @target: target VF number
 * @relay_id: relay message ID (must match message ID from the request)
 * @msg: response message (can't be NULL)
 * @len: length of the response message (in dwords, can't be 0)
 *
 * This function will embed and send provided `IOV Message`_ to the GuC.
 *
 * This function can only be used by driver running in SR-IOV PF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_reply_to_vf(struct intel_iov_relay *relay, u32 target,
				u32 relay_id, const u32 *msg, u32 len)
{
	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(!target && !I915_SELFTEST_ONLY(relay->selftest.enable_loopback));
	GEM_BUG_ON(len < GUC_HXG_MSG_MIN_LEN);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_REQUEST);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_EVENT);

	return relay_send(relay, target, relay_id, msg, len);
}

static int relay_send_success(struct intel_iov_relay *relay, u32 target,
			       u32 relay_id, u32 data)
{
	u32 msg[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		FIELD_PREP(GUC_HXG_RESPONSE_MSG_0_DATA0, data),
	};

	GEM_WARN_ON(!FIELD_FIT(GUC_HXG_RESPONSE_MSG_0_DATA0, data));

	return relay_send(relay, target, relay_id, msg, ARRAY_SIZE(msg));
}

/**
 * intel_iov_relay_reply_ack_to_vf - Send simple success response to VF.
 * @relay: the Relay struct
 * @target: target VF number (can't be 0)
 * @relay_id: relay message ID (must match message ID from the request)
 * @data: optional data
 *
 * This utility function will prepare success response message based on
 * given return data and and embed it in relay message for the GuC.
 *
 * This function can only be used by driver running in SR-IOV PF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_reply_ack_to_vf(struct intel_iov_relay *relay, u32 target,
				    u32 relay_id, u32 data)
{
	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(!target && !I915_SELFTEST_ONLY(relay->selftest.enable_loopback));

	return relay_send_success(relay, target, relay_id, data);
}

static u32 from_err_to_iov_error(int err)
{
	GEM_BUG_ON(err >= 0);
	return -err;
}

static u32 sanitize_iov_error(u32 error)
{
	/* XXX TBD if generic error codes will be allowed */
	if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST))
		error = IOV_ERROR_UNDISCLOSED;
	return error;
}

static u32 sanitize_iov_error_hint(u32 hint)
{
	/* XXX TBD if generic error codes will be allowed */
	if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST))
		hint = 0;
	return hint;
}

static int relay_send_failure(struct intel_iov_relay *relay, u32 target,
			      u32 relay_id, u32 error, u32 hint)
{
	u32 msg[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_FAILURE) |
		FIELD_PREP(GUC_HXG_FAILURE_MSG_0_HINT, hint) |
		FIELD_PREP(GUC_HXG_FAILURE_MSG_0_ERROR, error),
	};

	GEM_WARN_ON(!FIELD_FIT(GUC_HXG_FAILURE_MSG_0_ERROR, error));
	GEM_WARN_ON(!FIELD_FIT(GUC_HXG_FAILURE_MSG_0_HINT, hint));

	return relay_send(relay, target, relay_id, msg, ARRAY_SIZE(msg));
}

/**
 * intel_iov_relay_reply_err_to_vf - Send failure response to VF.
 * @relay: the Relay struct
 * @target: target VF number (can't be 0)
 * @relay_id: relay message ID (must match message ID from the request)
 * @err: errno code (must be < 0)
 *
 * This utility function will prepare failure response message based on
 * given error and hint and and embed it in relay message for the GuC.
 *
 * This function can only be used by driver running in SR-IOV PF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_reply_err_to_vf(struct intel_iov_relay *relay, u32 target,
				    u32 relay_id, int err)
{
	u32 error = from_err_to_iov_error(err);

	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(!target && !I915_SELFTEST_ONLY(relay->selftest.enable_loopback));

	return relay_send_failure(relay, target, relay_id,
				 sanitize_iov_error(error), 0);
}

/**
 * intel_iov_relay_reply_error_to_vf - Reply with error and hint to VF.
 * @relay: the Relay struct
 * @target: target VF number (can't be 0)
 * @relay_id: relay message ID (must match message ID from the request)
 * @error: error code
 * @hint: additional optional hint
 *
 * This utility function will prepare failure response message based on
 * given error and hint and and embed it in relay message for the GuC.
 *
 * This function can only be used by driver running in SR-IOV PF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_reply_error_to_vf(struct intel_iov_relay *relay, u32 target,
				      u32 relay_id, u16 error, u16 hint)
{
	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)));
	GEM_BUG_ON(!target);

	return relay_send_failure(relay, target, relay_id,
				 sanitize_iov_error(error),
				 sanitize_iov_error_hint(hint));
}

static int relay_send_and_wait(struct intel_iov_relay *relay, u32 target,
			       u32 relay_id, const u32 *msg, u32 len,
			       u32 *buf, u32 buf_size)
{
	unsigned long timeout = msecs_to_jiffies(RELAY_TIMEOUT);
	u32 action;
	u32 data0;
	struct pending_relay pending;
	int ret;
	long n;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_HOST);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_REQUEST);

	action = FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]);
	data0 = FIELD_GET(GUC_HXG_REQUEST_MSG_0_DATA0, msg[0]);
	RELAY_DEBUG(relay, "%s.%u to %u action %#x:%u\n",
		    hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])),
		    relay_id, target, action, data0);

	init_completion(&pending.done);
	pending.target = target;
	pending.fence = relay_id;
	pending.reply = -ENOMSG;
	pending.response = buf;
	pending.response_size = buf_size;

	/* list ordering does not need to match fence ordering */
	spin_lock(&relay->lock);
	list_add_tail(&pending.link, &relay->pending_relays);
	spin_unlock(&relay->lock);

retry:
	ret = relay_send(relay, target, relay_id, msg, len);
	if (unlikely(ret < 0))
		goto unlink;

wait:
	n = wait_for_completion_timeout(&pending.done, timeout);
	RELAY_DEBUG(relay, "%u.%u wait n=%ld\n", target, relay_id, n);
	if (unlikely(n == 0)) {
		ret = -ETIME;
		goto unlink;
	}

	RELAY_DEBUG(relay, "%u.%u reply=%d\n", target, relay_id, pending.reply);
	if (unlikely(pending.reply != 0)) {
		reinit_completion(&pending.done);
		ret = pending.reply;
		if (ret == -EAGAIN)
			goto retry;
		if (ret == -EBUSY)
			goto wait;
		if (ret > 0)
			ret = -ret;
		goto unlink;
	}

	GEM_BUG_ON(pending.response_size > buf_size);
	ret = pending.response_size;
	RELAY_DEBUG(relay, "%u.%u response %*ph\n", target, relay_id, 4 * ret, buf);

unlink:
	spin_lock(&relay->lock);
	list_del(&pending.link);
	spin_unlock(&relay->lock);

	if (unlikely(ret < 0)) {
		RELAY_PROBE_ERROR(relay, "Unsuccessful %s.%u %#x:%u to %u (%pe) %*ph\n",
				  hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])),
				  relay_id, action, data0, target, ERR_PTR(ret), 4 * len, msg);
	}

	return ret;
}

/**
 * intel_iov_relay_send_to_vf - Send message to VF.
 * @relay: the Relay struct
 * @target: target VF number
 * @data: request payload data
 * @dat_len: length of the payload data (in dwords, can be 0)
 * @buf: placeholder for the response message
 * @buf_size: size of the response message placeholder (in dwords)
 *
 * This function embed provided `IOV Message`_ into GuC relay.
 *
 * This function can only be used by driver running in SR-IOV PF mode.
 *
 * Return: Non-negative response length (in dwords) or
 *         a negative error code on failure.
 */
int intel_iov_relay_send_to_vf(struct intel_iov_relay *relay, u32 target,
			       const u32 *msg, u32 len, u32 *buf, u32 buf_size)
{
	u32 relay_type;
	u32 relay_id;

	GEM_BUG_ON(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(!target && !I915_SELFTEST_ONLY(relay->selftest.enable_loopback));
	GEM_BUG_ON(len < GUC_HXG_MSG_MIN_LEN);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_HOST);

	relay_type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]);
	relay_id = relay_get_next_fence(relay);

	if (relay_type == GUC_HXG_TYPE_EVENT)
		return relay_send(relay, target, relay_id, msg, len);

	GEM_BUG_ON(relay_type != GUC_HXG_TYPE_REQUEST);
	return relay_send_and_wait(relay, target, relay_id, msg, len, buf, buf_size);
}

/**
 * intel_iov_relay_send_to_pf - Send message to PF.
 * @relay: the Relay struct
 * @msg: message to be sent
 * @len: length of the message (in dwords, can't be 0)
 * @buf: placeholder for the response message
 * @buf_size: size of the response message placeholder (in dwords)
 *
 * This function embed provided `IOV Message`_ into GuC relay.
 *
 * This function can only be used by driver running in SR-IOV VF mode.
 *
 * Return: Non-negative response length (in dwords) or
 *         a negative error code on failure.
 */
int intel_iov_relay_send_to_pf(struct intel_iov_relay *relay,
			       const u32 *msg, u32 len, u32 *buf, u32 buf_size)
{
	u32 relay_type;
	u32 relay_id;

	GEM_BUG_ON(!IS_SRIOV_VF(relay_to_i915(relay)) &&
		   !I915_SELFTEST_ONLY(relay->selftest.disable_strict));
	GEM_BUG_ON(len < GUC_HXG_MSG_MIN_LEN);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_HOST);

	relay_type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]);
	relay_id = relay_get_next_fence(relay);

	if (relay_type == GUC_HXG_TYPE_EVENT)
		return relay_send(relay, 0, relay_id, msg, len);

	GEM_BUG_ON(relay_type != GUC_HXG_TYPE_REQUEST);
	return relay_send_and_wait(relay, 0, relay_id, msg, len, buf, buf_size);
}
ALLOW_ERROR_INJECTION(intel_iov_relay_send_to_pf, ERRNO);

static int relay_handle_reply(struct intel_iov_relay *relay, u32 origin,
			      u32 relay_id, int reply, const u32 *msg, u32 len)
{
	struct pending_relay *pending;
	int err = -ESRCH;

	spin_lock(&relay->lock);
	list_for_each_entry(pending, &relay->pending_relays, link) {
		if (pending->target != origin || pending->fence != relay_id) {
			RELAY_DEBUG(relay, "%u.%u still awaits response\n",
				    pending->target, pending->fence);
			continue;
		}
		err = 0;
		if (reply == 0) {
			if (unlikely(len > pending->response_size)) {
				reply = -ENOBUFS;
				err = -ENOBUFS;
			} else {
				pending->response[0] = FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, msg[0]);
				memcpy(pending->response + 1, msg + 1, 4 * (len - 1));
				pending->response_size = len;
			}
		}
		pending->reply = reply;
		complete_all(&pending->done);
		break;
	}
	spin_unlock(&relay->lock);

	return err;
}

static int relay_handle_failure(struct intel_iov_relay *relay, u32 origin,
				u32 relay_id, const u32 *msg, u32 len)
{
	int error = FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, msg[0]);
	u32 hint __maybe_unused = FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, msg[0]);

	GEM_BUG_ON(!len);
	RELAY_DEBUG(relay, "%u.%u error %#x (%pe) hint %u debug %*ph\n",
		    origin, relay_id, error, ERR_PTR(-error), hint, 4 * (len - 1), msg + 1);

	return relay_handle_reply(relay, origin, relay_id, error ?: -ERFKILL, NULL, 0);
}

static int relay_handle_request(struct intel_iov_relay *relay, u32 origin,
				u32 relay_id, const u32 *msg, u32 len)
{
	struct intel_iov *iov = relay_to_iov(relay);
	struct drm_i915_private *i915 = relay_to_i915(relay);
	struct intel_runtime_pm *rpm = &i915->runtime_pm;
	intel_wakeref_t wakeref = intel_runtime_pm_get(rpm);
	int err = -EOPNOTSUPP;

	if (intel_iov_is_pf(iov))
		err = intel_iov_service_process_msg(iov, origin,
						    relay_id, msg, len);

	if (unlikely(err < 0)) {
		u32 error = from_err_to_iov_error(err);

		RELAY_ERROR(relay, "Failed to handle %s.%u from %u (%pe) %*ph\n",
			    hxg_type_to_string(GUC_HXG_TYPE_REQUEST), relay_id,
			    origin, ERR_PTR(err), 4 * len, msg);
		err = relay_send_failure(relay, origin, relay_id,
					 origin ? sanitize_iov_error(error) : error, 0);
	}

	intel_runtime_pm_put(rpm, wakeref);
	return err;
}

static int relay_handle_event(struct intel_iov_relay *relay, u32 origin,
			      u32 relay_id, const u32 *msg, u32 len)
{
	return -EOPNOTSUPP;
}

static int relay_process_msg(struct intel_iov_relay *relay, u32 origin,
			     u32 relay_id, const u32 *relay_msg, u32 relay_len)
{
	u32 relay_type;
	int err;

	if (I915_SELFTEST_ONLY(!relay_selftest_process_msg(relay, origin, relay_id,
							   relay_msg, relay_len)))
		return 0;

	if (unlikely(relay_len < GUC_HXG_MSG_MIN_LEN))
		return -EPROTO;

	if (FIELD_GET(GUC_HXG_MSG_0_ORIGIN, relay_msg[0]) != GUC_HXG_ORIGIN_HOST)
		return -EPROTO;

	relay_type = FIELD_GET(GUC_HXG_MSG_0_TYPE, relay_msg[0]);
	RELAY_DEBUG(relay, "received %s.%u from %u = %*ph\n",
		    hxg_type_to_string(relay_type), relay_id, origin,
		    4 * relay_len, relay_msg);

	switch (relay_type) {
	case GUC_HXG_TYPE_REQUEST:
		err = relay_handle_request(relay, origin, relay_id, relay_msg, relay_len);
		break;
	case GUC_HXG_TYPE_EVENT:
		err = relay_handle_event(relay, origin, relay_id, relay_msg, relay_len);
		break;
	case GUC_HXG_TYPE_RESPONSE_SUCCESS:
		err = relay_handle_reply(relay, origin, relay_id, 0, relay_msg, relay_len);
		break;
	case GUC_HXG_TYPE_NO_RESPONSE_BUSY:
		err = relay_handle_reply(relay, origin, relay_id, -EBUSY, NULL, 0);
		break;
	case GUC_HXG_TYPE_NO_RESPONSE_RETRY:
		err = relay_handle_reply(relay, origin, relay_id, -EAGAIN, NULL, 0);
		break;
	case GUC_HXG_TYPE_RESPONSE_FAILURE:
		err = relay_handle_failure(relay, origin, relay_id, relay_msg, relay_len);
		break;
	default:
		err = -EBADRQC;
	}

	if (unlikely(err))
		RELAY_ERROR(relay, "Failed to process %s.%u from %u (%pe) %*ph\n",
			    hxg_type_to_string(relay_type), relay_id, origin,
			    ERR_PTR(err),  4 * relay_len, relay_msg);

	return err;
}

/**
 * intel_iov_relay_process_guc2pf - Handle relay notification message from the GuC.
 * @relay: the Relay struct
 * @msg: message to be handled
 * @len: length of the message (in dwords)
 *
 * This function will handle RELAY messages received from the GuC.
 *
 * This function can only be used if driver is running in SR-IOV PF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_process_guc2pf(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	u32 origin, relay_id;

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
	if (unlikely(!IS_ERR_OR_NULL(relay->selftest.guc2pf))) {
		int ret = relay->selftest.guc2pf(relay, msg, len);

		if (ret != -ENOTTY) {
			relay->selftest.guc2pf = ERR_PTR(ret < 0 ? ret : 0);
			return ret;
		}
	}
#endif

	if (unlikely(!IS_SRIOV_PF(relay_to_i915(relay)) &&
		     !I915_SELFTEST_ONLY(relay->selftest.disable_strict)))
		return -EPERM;

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) != GUC_ACTION_GUC2PF_RELAY_FROM_VF);

	if (unlikely(len < GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN))
		return -EPROTO;

	if (unlikely(len > GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN))
		return -EMSGSIZE;

	if (unlikely(FIELD_GET(GUC_HXG_EVENT_MSG_0_DATA0, msg[0])))
		return -EPFNOSUPPORT;

	origin = FIELD_GET(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, msg[1]);
	relay_id = FIELD_GET(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID, msg[2]);

	if (unlikely(!origin))
		return -EPROTO;

	return relay_process_msg(relay, origin, relay_id,
				 msg + GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN,
				 len - GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN);
}

/**
 * intel_iov_relay_process_guc2vf - Handle relay notification message from the GuC.
 * @relay: the Relay struct
 * @msg: message to be handled
 * @len: length of the message (in dwords)
 *
 * This function will handle RELAY messages received from the GuC.
 *
 * This function can only be used if driver is running in SR-IOV VF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_relay_process_guc2vf(struct intel_iov_relay *relay, const u32 *msg, u32 len)
{
	struct drm_i915_private *i915 = relay_to_i915(relay);
	u32 relay_id;

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
	if (unlikely(!IS_ERR_OR_NULL(relay->selftest.guc2vf))) {
		int ret = relay->selftest.guc2vf(relay, msg, len);

		if (ret != -ENOTTY) {
			relay->selftest.guc2vf = ERR_PTR(ret < 0 ? ret : 0);
			return ret;
		}
	}
#endif

	if (unlikely(!IS_SRIOV_VF(i915)) &&
		     !(I915_SELFTEST_ONLY(relay->selftest.disable_strict) ||
		       I915_SELFTEST_ONLY(relay->selftest.enable_loopback)))
		return -EPERM;

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) != GUC_ACTION_GUC2VF_RELAY_FROM_PF);

	if (unlikely(len < GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN))
		return -EPROTO;

	if (unlikely(len > GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN))
		return -EMSGSIZE;

	if (unlikely(FIELD_GET(GUC_HXG_EVENT_MSG_0_DATA0, msg[0])))
		return -EPFNOSUPPORT;

	relay_id = FIELD_GET(GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID, msg[1]);

	return relay_process_msg(relay, 0, relay_id,
				 msg + GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN,
				 len - GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#undef intel_guc_send
#undef intel_guc_send_nb
#undef intel_guc_ct_send
#include "selftests/selftest_util_iov_relay.c"
#include "selftests/selftest_mock_iov_relay.c"
#include "selftests/selftest_live_iov_relay.c"
#include "selftests/selftest_perf_iov_relay.c"
#endif
