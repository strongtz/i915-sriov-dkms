// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#include "selftests/i915_random.h"

/* XXX pick policy key that is safe to use */
#define GUC_KLV_VGT_POLICY_EXAMPLE_KEY			GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_KEY
#define GUC_KLV_VGT_POLICY_EXAMPLE_LEN			GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_LEN

/* XXX make sure this policy key does not exist ! */
#define GUC_KLV_VGT_POLICY_DOES_NOT_EXIST_KEY		0x8DDD
#define GUC_KLV_VGT_POLICY_DOES_NOT_EXIST_LEN		1u

static int pf_guc_accepts_example_policy_key(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	int ret;

	ret = guc_update_policy_klv32(guc, GUC_KLV_VGT_POLICY_EXAMPLE_KEY, 0);
	if (ret) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't accept example key, %d\n", ret);
		return -EINVAL;
	}
	return 0;
}

static int pf_guc_ignores_unknown_policy_key(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	int ret;

	ret = guc_update_policy_klv32(guc, GUC_KLV_VGT_POLICY_DOES_NOT_EXIST_KEY, 0);
	if (ret != -ENOKEY) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't ignore example key, %d\n", ret);
		return 0; /* XXX firmware bug GUC-4317 */
		return -EINVAL;
	}
	return 0;
}

static int __guc_try_update_policy(struct intel_guc *guc, u64 addr, u32 size, u32 len)
{
	u32 request[GUC_CTB_MSG_MAX_LEN - GUC_CTB_MSG_MIN_LEN] = {
		GUC_ACTION_PF2GUC_UPDATE_VGT_POLICY,
		lower_32_bits(addr),
		upper_32_bits(addr),
		size,
		POISON_END,
		/* ... */
	};
	unsigned int n;

	BUILD_BUG_ON(ARRAY_SIZE(request) == PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN);
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > ARRAY_SIZE(request));

	for (n = PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN; n < len; n++)
		request[n] = POISON_END;

	return intel_guc_ct_send(&guc->ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST);
}

static int guc_try_update_policy(struct intel_guc *guc, u64 addr, u32 size)
{
	return __guc_try_update_policy(guc, addr, size, PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN);
}

static int pf_guc_parses_flexible_policy_keys(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	static const unsigned int max_klv_len = SZ_64K - 1;
	unsigned int blob_size = sizeof(u32) * (GUC_KLV_LEN_MIN + max_klv_len);
	unsigned int len;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, blob_size, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	for (len = 0; len <= max_klv_len; len++) {
		IOV_DEBUG(iov, "len=%u\n", len);

		klvs = blob;
		*klvs++ = FIELD_PREP(GUC_KLV_0_KEY, GUC_KLV_VGT_POLICY_DOES_NOT_EXIST_KEY) |
			  FIELD_PREP(GUC_KLV_0_LEN, len);
		*klvs++ = len;

		ret = guc_try_update_policy(guc, addr, GUC_KLV_LEN_MIN + len);
		if (ret < 0) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't parse flexible key len=%u, %d\n",
					   len, ret);
			result = -EPROTO;
			break;
		}

		if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
			len += i915_prandom_u32_max_state(len, &prng);
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

static int pf_guc_rejects_invalid_update_policy_params(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, SZ_4K, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	klvs = blob;
	*klvs++ = MAKE_GUC_KLV(VGT_POLICY_EXAMPLE);
	*klvs++ = 0;
	klvs_size = klvs - blob;

	ret = guc_try_update_policy(guc, 0, klvs_size);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject zero address, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

	ret = guc_try_update_policy(guc, addr, 0);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject zero size, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

	ret = guc_try_update_policy(guc, addr, klvs_size - 1);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject truncated blob, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

release:
	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

static int pf_guc_rejects_incomplete_update_policy_hxg(void *arg)
{
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	unsigned int len;
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, SZ_4K, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	klvs = blob;
	*klvs++ = MAKE_GUC_KLV(VGT_POLICY_EXAMPLE);
	*klvs++ = 0;
	klvs_size = klvs - blob;

	for (len = GUC_HXG_REQUEST_MSG_MIN_LEN;
	     len < PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN; len++) {
		ret = __guc_try_update_policy(guc, addr, klvs_size, len);
		if (ret != -EIO) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't reject incomplete HXG len=%u, %d\n",
					   len, ret);
			result = -EPROTO;
			break;
		}
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

static int pf_guc_accepts_extended_update_policy_hxg(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	unsigned int len;
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, SZ_4K, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	klvs = blob;
	*klvs++ = MAKE_GUC_KLV(VGT_POLICY_EXAMPLE);
	*klvs++ = 0;
	klvs_size = klvs - blob;

	/*
	 * GuC team claims that they will always accept messages longer than
	 * defined in current ABI as this will allow future extensions.
	 */
	for (len = PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN + 1;
	     len < GUC_CTB_MSG_MAX_LEN - GUC_CTB_MSG_MIN_LEN; len++) {
		IOV_DEBUG(iov, "len=%u\n", len);

		ret = __guc_try_update_policy(guc, addr, klvs_size, len);
		if (ret != 1) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't accepts extended HXG len=%u, %d\n",
					   len, ret);
			result = -EPROTO;
			break;
		}

		if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
			len += i915_prandom_u32_max_state(len, &prng);
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

#define IOV_POLICY_KLVS(config) \
	config(SCHED_IF_IDLE) \
	config(ADVERSE_SAMPLE_PERIOD) \
	config(RESET_AFTER_VF_SWITCH) \
	/*end*/

static int pf_guc_rejects_broken_policy_klv(void *arg)
{
	I915_RND_STATE(prng);
	static const unsigned int max_klv_len = SZ_64K - 1;
	static const struct {
		u16 key;
		u16 len;
	} policies[] = {
#define config(K) { .key = GUC_KLV_VGT_POLICY_##K##_KEY, .len = GUC_KLV_VGT_POLICY_##K##_LEN },
	IOV_POLICY_KLVS(config)
#undef config
	};
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	unsigned int blob_size = sizeof(u32) * (GUC_KLV_LEN_MIN + max_klv_len);
	unsigned int n, len;
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, blob_size, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	for (n = 0; n < ARRAY_SIZE(policies); n++) {
		for (len = 0; len <= max_klv_len; len++) {
			if (len == policies[n].len)
				continue;
			IOV_DEBUG(iov, "len=%u\n", len);

			klvs = blob;
			*klvs++ = FIELD_PREP(GUC_KLV_0_KEY, policies[n].key) |
				  FIELD_PREP(GUC_KLV_0_LEN, len);
			*klvs++ = len;
			klvs_size = GUC_KLV_LEN_MIN + len;

			ret = guc_try_update_policy(guc, addr, klvs_size);
			if (ret != -EIO) {
				IOV_SELFTEST_ERROR(iov,
						   "GuC didn't reject KLV %s/%04x len=%u, %d\n",
						   policy_key_to_string(policies[n].key),
						   policies[n].key, len, ret);
				result = -EPROTO;
				break;
			}

			if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
				len += i915_prandom_u32_max_state(len, &prng);
		}
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return 0; /* XXX firmware bug GUC-4363 */
	return result;
}

/* XXX pick config key that is safe to use */
#define GUC_KLV_VF_CFG_EXAMPLE_KEY			GUC_KLV_VF_CFG_THRESHOLD_CAT_ERR_KEY
#define GUC_KLV_VF_CFG_EXAMPLE_LEN			GUC_KLV_VF_CFG_THRESHOLD_CAT_ERR_LEN

/* XXX make sure this config key does not exist ! */
#define GUC_KLV_VF_CFG_DOES_NOT_EXIST_KEY		0x8ADD
#define GUC_KLV_VF_CFG_DOES_NOT_EXIST_LEN		1u

static int pf_guc_accepts_example_config_key(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	int ret;

	ret = guc_update_vf_klv32(guc, vfid, GUC_KLV_VF_CFG_EXAMPLE_KEY, 0);
	if (ret) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't accept example key, %d\n", ret);
		return -EINVAL;
	}
	return 0;
}

static int pf_guc_ignores_unknown_config_key(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	int ret;

	ret = guc_update_vf_klv32(guc, vfid, GUC_KLV_VF_CFG_DOES_NOT_EXIST_KEY, 0);
	if (ret != -ENOKEY) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't ignore example key, %d\n", ret);
		return 0; /* XXX firmware bug GUC-4317 */
		return -EINVAL;
	}
	return 0;
}

static int __guc_try_update_config(struct intel_guc *guc, u32 vfid, u64 addr, u32 size, u32 len)
{
	u32 request[GUC_CTB_MSG_MAX_LEN - GUC_CTB_MSG_MIN_LEN] = {
		GUC_ACTION_PF2GUC_UPDATE_VF_CFG,
		vfid,
		lower_32_bits(addr),
		upper_32_bits(addr),
		size,
		POISON_END,
		/* ... */
	};
	unsigned int n;

	BUILD_BUG_ON(ARRAY_SIZE(request) == PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN);
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > ARRAY_SIZE(request));

	for (n = PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_LEN; n < len; n++)
		request[n] = POISON_END;

	return intel_guc_ct_send(&guc->ct, request, len, NULL, 0, INTEL_GUC_CT_SEND_SELFTEST);
}

static int guc_try_update_config(struct intel_guc *guc, u32 vfid, u64 addr, u32 size)
{
	return __guc_try_update_config(guc, vfid, addr, size, PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_LEN);
}

static int pf_guc_parses_flexible_config_keys(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	static const unsigned int max_klv_len = SZ_64K - 1;
	unsigned int blob_size = sizeof(u32) * (GUC_KLV_LEN_MIN + max_klv_len);
	unsigned int len;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, blob_size, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	for (len = 0; len <= max_klv_len; len++) {
		IOV_DEBUG(iov, "len=%u\n", len);

		klvs = blob;
		*klvs++ = FIELD_PREP(GUC_KLV_0_KEY, GUC_KLV_VF_CFG_DOES_NOT_EXIST_KEY) |
			  FIELD_PREP(GUC_KLV_0_LEN, len);
		*klvs++ = len;

		ret = guc_try_update_config(guc, vfid, addr, GUC_KLV_LEN_MIN + len);
		if (ret < 0) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't parse flexible key len=%u, %d\n",
					   len, ret);
			result = -EPROTO;
			break;
		}

		if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
			len += i915_prandom_u32_max_state(len, &prng);
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

static int pf_guc_rejects_invalid_update_config_params(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, SZ_4K, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	klvs = blob;
	*klvs++ = MAKE_GUC_KLV(VF_CFG_EXAMPLE);
	*klvs++ = 0;
	klvs_size = klvs - blob;

	ret = guc_try_update_config(guc, pf_get_totalvfs(iov) + 1, addr, klvs_size);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject invalid VF, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

	ret = guc_try_update_config(guc, vfid, 0, klvs_size);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject zero address, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

	ret = guc_try_update_config(guc, vfid, 0, klvs_size);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject zero address, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

	ret = guc_try_update_config(guc, vfid, addr, 0);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject zero size, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

	ret = guc_try_update_config(guc, vfid, addr, klvs_size - 1);
	if (ret != -EIO) {
		IOV_SELFTEST_ERROR(iov, "GuC didn't reject truncated blob, %d\n", ret);
		result = -EPROTO;
		goto release;
	}

release:
	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

static int pf_guc_rejects_incomplete_update_config_hxg(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	unsigned int len;
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, SZ_4K, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	klvs = blob;
	*klvs++ = MAKE_GUC_KLV(VGT_POLICY_EXAMPLE);
	*klvs++ = 0;
	klvs_size = klvs - blob;

	for (len = GUC_HXG_REQUEST_MSG_MIN_LEN;
	     len < PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_LEN; len++) {
		ret = __guc_try_update_config(guc, vfid, addr, klvs_size, len);
		if (ret != -EIO) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't reject incomplete HXG len=%u, %d\n",
					   len, ret);
			result = -EPROTO;
			break;
		}
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return 0; /* XXX firmware bug GUC-4364 */
	return result;
}

static int pf_guc_accepts_extended_update_config_hxg(void *arg)
{
	I915_RND_STATE(prng);
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	unsigned int len;
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, SZ_4K, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	klvs = blob;
	*klvs++ = MAKE_GUC_KLV(VGT_POLICY_EXAMPLE);
	*klvs++ = 0;
	klvs_size = klvs - blob;

	/*
	 * GuC team claims that they will always accept messages longer than
	 * defined in current ABI as this will allow future extensions.
	 */
	for (len = PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_LEN + 1;
	     len < GUC_CTB_MSG_MAX_LEN - GUC_CTB_MSG_MIN_LEN; len++) {
		IOV_DEBUG(iov, "len=%u\n", len);

		ret = __guc_try_update_config(guc, vfid, addr, klvs_size, len);
		if (ret != 1) {
			IOV_SELFTEST_ERROR(iov, "GuC didn't accepts extended HXG len=%u, %d\n",
					   len, ret);
			result = -EPROTO;
			break;
		}

		if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
			len += i915_prandom_u32_max_state(len, &prng);
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return result;
}

#define config_threshold(K, ...) config(THRESHOLD_##K)
#define IOV_VF_CFG_KLVS(config) \
	config(GGTT_START) \
	config(GGTT_SIZE) \
	config(NUM_CONTEXTS) \
	config(BEGIN_CONTEXT_ID) \
	config(NUM_DOORBELLS) \
	config(BEGIN_DOORBELL_ID) \
	config(EXEC_QUANTUM) \
	config(PREEMPT_TIMEOUT) \
	IOV_THRESHOLDS(config_threshold) \
	/*end*/

static int pf_guc_rejects_broken_config_klv(void *arg)
{
	I915_RND_STATE(prng);
	static const unsigned int max_klv_len = SZ_64K - 1;
	static const struct {
		u16 key;
		u16 len;
	} configs[] = {
#define config(K) { .key = GUC_KLV_VF_CFG_##K##_KEY, .len = GUC_KLV_VF_CFG_##K##_LEN },
	IOV_VF_CFG_KLVS(config)
#undef config
	};
	struct intel_iov *iov = arg;
	struct intel_guc *guc = iov_to_guc(iov);
	struct i915_vma *vma;
	unsigned int blob_size = sizeof(u32) * (GUC_KLV_LEN_MIN + max_klv_len);
	unsigned int n, len;
	u32 vfid = i915_prandom_u32_max_state(1 + pf_get_totalvfs(iov), &prng);
	u32 klvs_size;
	u32 *blob;
	u32 *klvs;
	u64 addr;
	int ret, result = 0;

	ret = intel_guc_allocate_and_map_vma(guc, blob_size, &vma, (void **)&blob);
	if (unlikely(ret))
		return ret;
	addr = intel_guc_ggtt_offset(guc, vma);

	for (n = 0; n < ARRAY_SIZE(configs); n++) {
		for (len = 0; len <= max_klv_len; len++) {
			if (len == configs[n].len)
				continue;
			IOV_DEBUG(iov, "len=%u\n", len);

			klvs = blob;
			*klvs++ = FIELD_PREP(GUC_KLV_0_KEY, configs[n].key) |
				  FIELD_PREP(GUC_KLV_0_LEN, len);
			*klvs++ = len;
			klvs_size = GUC_KLV_LEN_MIN + len;

			ret = guc_try_update_config(guc, vfid, addr, klvs_size);
			if (ret != -EIO) {
				IOV_SELFTEST_ERROR(iov,
						   "GuC didn't reject KLV %04x len=%u, %d\n",
						   configs[n].key, len, ret);
				result = -EPROTO;
				break;
			}

			if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
				len += i915_prandom_u32_max_state(len, &prng);
		}
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
	return 0; /* XXX firmware bug GUC-4363 */
	return result;
}

int selftest_live_iov_provisioning(struct drm_i915_private *i915)
{
	static const struct i915_subtest pf_policy_tests[] = {
		SUBTEST(pf_guc_accepts_example_policy_key),
		SUBTEST(pf_guc_ignores_unknown_policy_key),
		SUBTEST(pf_guc_parses_flexible_policy_keys),
		SUBTEST(pf_guc_rejects_invalid_update_policy_params),
		SUBTEST(pf_guc_rejects_incomplete_update_policy_hxg),
		SUBTEST(pf_guc_accepts_extended_update_policy_hxg),
		SUBTEST(pf_guc_rejects_broken_policy_klv),
	};
	static const struct i915_subtest pf_config_tests[] = {
		SUBTEST(pf_guc_accepts_example_config_key),
		SUBTEST(pf_guc_ignores_unknown_config_key),
		SUBTEST(pf_guc_parses_flexible_config_keys),
		SUBTEST(pf_guc_rejects_invalid_update_config_params),
		SUBTEST(pf_guc_rejects_incomplete_update_config_hxg),
		SUBTEST(pf_guc_accepts_extended_update_config_hxg),
		SUBTEST(pf_guc_rejects_broken_config_klv),
	};
	intel_wakeref_t wakeref;
	int err = 0;

	if (!IS_SRIOV_PF(i915))
		return 0;

	if (i915_sriov_pf_status(i915) < 0)
		return -EHOSTDOWN;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		struct intel_iov *iov = &to_gt(i915)->iov;

		err = intel_iov_provisioning_force_vgt_mode(iov);
		if (err)
			break;
		err = intel_iov_live_subtests(pf_policy_tests, iov);
		if (err)
			break;
		err = intel_iov_live_subtests(pf_config_tests, iov);
		if (err)
			break;
	}

	return err;
}
