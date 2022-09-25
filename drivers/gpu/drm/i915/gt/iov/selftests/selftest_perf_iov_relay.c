// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#define SELFTEST_RELAY_PERF_LOOP		100
#define SELFTEST_RELAY_PERF_TIME_MS		100

static int pf_loopback_to_vf_delay(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg[] = {
		MSG_IOV_SELFTEST_RELAY(SELFTEST_RELAY_OPCODE_NOP),
	};
	u32 buf[GUC_HXG_MSG_MIN_LEN];
	s64 delay_total = 0, delay_min = 0, delay_max = 0;
	unsigned int n;
	int ret = 0;

	iov->relay.selftest.enable_loopback = 1;

	for (n = 0; n < SELFTEST_RELAY_PERF_LOOP; n++) {
		ktime_t start = ktime_get();
		s64 delta;

		ret = intel_iov_relay_send_to_vf(&iov->relay, PFID, msg, ARRAY_SIZE(msg),
						 buf, ARRAY_SIZE(buf));

		if (ret != GUC_HXG_MSG_MIN_LEN)
			break;

		delta = ktime_us_delta(ktime_get(), start);
		delay_total += delta;

		delay_min = n ? min(delta, delay_min) : delta;
		delay_max = max(delta, delay_max);
	}

	iov->relay.selftest.enable_loopback = 0;

	if (n < SELFTEST_RELAY_PERF_LOOP)
		return -ENODATA;

	dev_info(iov_to_dev(iov), "delay %llu us (min %llu max %llu over %u iterations)\n",
		 div_s64(delay_total, SELFTEST_RELAY_PERF_LOOP),
		 delay_min, delay_max, SELFTEST_RELAY_PERF_LOOP);

	return 0;
}

static int pf_loopback_to_vf_throughput(void *arg)
{
	struct intel_iov *iov = arg;
	u32 msg[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA] = {
		MSG_IOV_SELFTEST_RELAY(SELFTEST_RELAY_OPCODE_NOP),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		/* ... */
	};
	u32 buf[GUC_HXG_MSG_MIN_LEN];
	ktime_t finish;
	u64 throughput = 0;
	u64 counter = 0;
	int ret = 0;

	iov->relay.selftest.enable_loopback = 1;
	finish = ktime_add_ms(ktime_get(), SELFTEST_RELAY_PERF_TIME_MS);

	while (ktime_before(ktime_get(), finish)) {
		ret = intel_iov_relay_send_to_vf(&iov->relay, PFID, msg, ARRAY_SIZE(msg),
						 buf, ARRAY_SIZE(buf));

		if (ret != GUC_HXG_MSG_MIN_LEN)
			break;

		throughput += ARRAY_SIZE(msg);
		throughput += ret;
		counter++;
	}

	iov->relay.selftest.enable_loopback = 0;

	if (ret != GUC_HXG_MSG_MIN_LEN)
		return -ENODATA;

	dev_info(iov_to_dev(iov), "throughput %llu bytes/s (%llu relays/s)\n",
		 div_s64(throughput * sizeof(u32) * MSEC_PER_SEC,
			 SELFTEST_RELAY_PERF_TIME_MS), counter);

	return 0;
}

static int relay_to_pf_delay(struct intel_iov *iov)
{
	u32 msg[] = {
		MSG_IOV_SELFTEST_RELAY(SELFTEST_RELAY_OPCODE_NOP),
	};
	u32 buf[GUC_HXG_MSG_MIN_LEN];
	s64 delay_total = 0, delay_min = 0, delay_max = 0;
	unsigned int n;
	int ret = 0;

	for (n = 0; n < SELFTEST_RELAY_PERF_LOOP; n++) {
		ktime_t start = ktime_get();
		s64 delta;

		ret = intel_iov_relay_send_to_pf(&iov->relay, msg, ARRAY_SIZE(msg),
						 buf, ARRAY_SIZE(buf));

		if (ret != GUC_HXG_MSG_MIN_LEN)
			break;

		delta = ktime_us_delta(ktime_get(), start);
		delay_total += delta;

		delay_min = n ? min(delta, delay_min) : delta;
		delay_max = max(delta, delay_max);
	}

	if (n < SELFTEST_RELAY_PERF_LOOP)
		return -ENODATA;

	dev_info(iov_to_dev(iov), "delay %llu us (min %llu max %llu over %u iterations)\n",
		 div_s64(delay_total, SELFTEST_RELAY_PERF_LOOP),
		 delay_min, delay_max, SELFTEST_RELAY_PERF_LOOP);

	return 0;
}

static int pf_loopback_to_pf_delay(void *arg)
{
	struct intel_iov *iov = arg;
	int err;

	iov->relay.selftest.disable_strict = 1;
	iov->relay.selftest.enable_loopback = 1;

	err = relay_to_pf_delay(iov);

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;

	return err;
}

static int relay_to_pf_throughput(struct intel_iov *iov)
{
	u32 msg[PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA] = {
		MSG_IOV_SELFTEST_RELAY(SELFTEST_RELAY_OPCODE_NOP),
		FIELD_PREP(GUC_HXG_REQUEST_MSG_n_DATAn, SELFTEST_RELAY_DATA),
		/* ... */
	};
	u32 buf[GUC_HXG_MSG_MIN_LEN];
	ktime_t finish;
	u64 throughput = 0;
	u64 counter = 0;
	int ret = 0;

	finish = ktime_add_ms(ktime_get(), SELFTEST_RELAY_PERF_TIME_MS);

	while (ktime_before(ktime_get(), finish)) {
		ret = intel_iov_relay_send_to_pf(&iov->relay, msg, ARRAY_SIZE(msg),
						 buf, ARRAY_SIZE(buf));

		if (ret != GUC_HXG_MSG_MIN_LEN)
			break;

		throughput += ARRAY_SIZE(msg);
		throughput += ret;
		counter++;
	}

	if (ret != GUC_HXG_MSG_MIN_LEN)
		return -ENODATA;

	dev_info(iov_to_dev(iov), "throughput %llu bytes/s (%llu relays/s)\n",
		 div_s64(throughput * sizeof(u32) * MSEC_PER_SEC,
			 SELFTEST_RELAY_PERF_TIME_MS), counter);

	return 0;
}

static int pf_loopback_to_pf_throughput(void *arg)
{
	struct intel_iov *iov = arg;
	int err;

	iov->relay.selftest.disable_strict = 1;
	iov->relay.selftest.enable_loopback = 1;

	err = relay_to_pf_throughput(iov);

	iov->relay.selftest.enable_loopback = 0;
	iov->relay.selftest.disable_strict = 0;

	return err;
}

static int vf_to_pf_delay(void *arg)
{
	return relay_to_pf_delay(arg);
}

static int vf_to_pf_throughput(void *arg)
{
	return relay_to_pf_throughput(arg);
}

int selftest_perf_iov_relay(struct drm_i915_private *i915)
{
	static const struct i915_subtest pf_tests[] = {
		SUBTEST(pf_loopback_to_vf_delay),
		SUBTEST(pf_loopback_to_vf_throughput),
		SUBTEST(pf_loopback_to_pf_delay),
		SUBTEST(pf_loopback_to_pf_throughput),
	};
	static const struct i915_subtest vf_tests[] = {
		SUBTEST(vf_to_pf_delay),
		SUBTEST(vf_to_pf_throughput),
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
			intel_iov_provisioning_force_vgt_mode(iov);
			err = intel_iov_live_subtests(pf_tests, iov);
		} else if (IS_SRIOV_VF(i915)) {
			err = intel_iov_live_subtests(vf_tests, iov);
		}
		if (err)
			break;
	}

	return err;
}
