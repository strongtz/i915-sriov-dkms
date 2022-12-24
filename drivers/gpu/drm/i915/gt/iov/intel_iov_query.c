// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_print.h>
#include <linux/bitfield.h>
#include <linux/crc32.h>

#include "abi/iov_actions_abi.h"
#include "abi/iov_actions_mmio_abi.h"
#include "abi/iov_version_abi.h"
#include "gt/intel_gt_regs.h"
#include "gt/uc/abi/guc_actions_vf_abi.h"
#include "gt/uc/abi/guc_klvs_abi.h"
#include "gt/uc/abi/guc_version_abi.h"
#include "i915_drv.h"
#include "intel_iov_relay.h"
#include "intel_iov_utils.h"
#include "intel_iov_types.h"
#include "intel_iov_query.h"

static int guc_action_vf_reset(struct intel_guc *guc)
{
	u32 request[GUC_HXG_REQUEST_MSG_MIN_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_VF_RESET),
	};
	int ret;

	ret = intel_guc_send_mmio(guc, request, ARRAY_SIZE(request), NULL, 0);

	return ret > 0 ? -EPROTO : ret;
}

static int vf_reset_guc_state(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = guc_action_vf_reset(guc);
	if (unlikely(err))
		IOV_PROBE_ERROR(iov, "Failed to reset GuC state (%pe)\n",
				ERR_PTR(err));

	return err;
}

static int guc_action_match_version(struct intel_guc *guc, u32 *branch,
				    u32 *major, u32 *minor, u32 *patch)
{
	u32 request[VF2GUC_MATCH_VERSION_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_VF2GUC_MATCH_VERSION),
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_BRANCH,
			   *branch) |
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MAJOR,
			   *major) |
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MINOR,
			   *minor),
	};
	u32 response[VF2GUC_MATCH_VERSION_RESPONSE_MSG_LEN];
	int ret;

	ret = intel_guc_send_mmio(guc, request, ARRAY_SIZE(request),
				  response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		return ret;

	GEM_BUG_ON(ret != VF2GUC_MATCH_VERSION_RESPONSE_MSG_LEN);
	if (unlikely(FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	*branch = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_BRANCH, response[1]);
	*major = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MAJOR, response[1]);
	*minor = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MINOR, response[1]);
	*patch = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_PATCH, response[1]);

	return 0;
}

static int vf_handshake_with_guc(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 branch, major, minor, patch;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	/* XXX for now, all platforms use same latest version */
	branch = GUC_VERSION_BRANCH_ANY;
	major = GUC_VF_VERSION_LATEST_MAJOR;
	minor = GUC_VF_VERSION_LATEST_MINOR;

	err = guc_action_match_version(guc, &branch, &major, &minor, &patch);
	if (unlikely(err))
		goto fail;

	/* XXX we only support one version, there must be a match */
	if (major != GUC_VF_VERSION_LATEST_MAJOR || minor != GUC_VF_VERSION_LATEST_MINOR)
		goto fail;

	dev_info(iov_to_dev(iov), "%s interface version %u.%u.%u.%u\n",
		 intel_uc_fw_type_repr(guc->fw.type),
		 branch, major, minor, patch);

	intel_uc_fw_set_preloaded(&guc->fw, major, minor);
	return 0;

fail:
	IOV_PROBE_ERROR(iov, "Unable to confirm version %u.%u (%pe)\n",
			major, minor, ERR_PTR(err));

	/* try again with *any* just to query which version is supported */
	branch = GUC_VERSION_BRANCH_ANY;
	major = GUC_VERSION_MAJOR_ANY;
	minor = GUC_VERSION_MINOR_ANY;
	if (!guc_action_match_version(guc, &branch, &major, &minor, &patch))
		IOV_PROBE_ERROR(iov, "Found interface version %u.%u.%u.%u\n",
				branch, major, minor, patch);

	return err;
}

/**
 * intel_iov_query_bootstrap - Query interface version data over MMIO.
 * @iov: the IOV struct
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_query_bootstrap(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = vf_reset_guc_state(iov);
	if (unlikely(err))
		return err;

	err = vf_handshake_with_guc(iov);
	if (unlikely(err))
		return err;

	return 0;
}

static int guc_action_query_single_klv(struct intel_guc *guc, u32 key,
				       u32 *value, u32 value_len)
{
	u32 request[VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_VF2GUC_QUERY_SINGLE_KLV),
		FIELD_PREP(VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_1_KEY, key),
	};
	u32 response[VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MAX_LEN];
	u32 length;
	int ret;

	ret = intel_guc_send_mmio(guc, request, ARRAY_SIZE(request),
				  response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		return ret;

	GEM_BUG_ON(ret != VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MAX_LEN);
	if (unlikely(FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	length = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_LENGTH, response[0]);
	if (unlikely(length > value_len))
		return -EOVERFLOW;
	if (unlikely(length < value_len))
		return -ENODATA;

	GEM_BUG_ON(length != value_len);
	switch (value_len) {
	default:
		GEM_BUG_ON(value_len);
		return -EINVAL;
	case 3:
		value[2] = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_3_VALUE96, response[3]);
		fallthrough;
	case 2:
		value[1] = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_2_VALUE64, response[2]);
		fallthrough;
	case 1:
		value[0] = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_1_VALUE32, response[1]);
		fallthrough;
	case 0:
		break;
	}

	return 0;
}

static int guc_action_query_single_klv32(struct intel_guc *guc, u32 key, u32 *value32)
{
	return guc_action_query_single_klv(guc, key, value32, 1);
}

static int guc_action_query_single_klv64(struct intel_guc *guc, u32 key, u64 *value64)
{
	u32 value[2];
	int err;

	err = guc_action_query_single_klv(guc, key, value, ARRAY_SIZE(value));
	if (unlikely(err))
		return err;

	*value64 = (u64)value[1] << 32 | value[0];
	return 0;
}

static int vf_get_ggtt_info(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u64 start, size;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = guc_action_query_single_klv64(guc, GUC_KLV_VF_CFG_GGTT_START_KEY, &start);
	if (unlikely(err))
		return err;

	err = guc_action_query_single_klv64(guc, GUC_KLV_VF_CFG_GGTT_SIZE_KEY, &size);
	if (unlikely(err))
		return err;

	IOV_DEBUG(iov, "GGTT %#llx-%#llx = %lluK\n",
		  start, start + size - 1, size / SZ_1K);

	if (iov->vf.config.ggtt_size && iov->vf.config.ggtt_size != size) {
		IOV_ERROR(iov, "Unexpected GGTT reassignment: %lluK != %lluK\n",
			  size / SZ_1K, iov->vf.config.ggtt_size / SZ_1K);
		return -EREMCHG;
	}

	iov->vf.config.ggtt_base = start;
	iov->vf.config.ggtt_size = size;

	return iov->vf.config.ggtt_size ? 0 : -ENODATA;
}

static int vf_get_submission_cfg(struct intel_iov *iov)
{
	struct intel_guc *guc = iov_to_guc(iov);
	u32 num_ctxs, num_dbs;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = guc_action_query_single_klv32(guc, GUC_KLV_VF_CFG_NUM_CONTEXTS_KEY, &num_ctxs);
	if (unlikely(err))
		return err;

	err = guc_action_query_single_klv32(guc, GUC_KLV_VF_CFG_NUM_DOORBELLS_KEY, &num_dbs);
	if (unlikely(err))
		return err;

	IOV_DEBUG(iov, "CTXs %u DBs %u\n", num_ctxs, num_dbs);

	if (iov->vf.config.num_ctxs && iov->vf.config.num_ctxs != num_ctxs) {
		IOV_ERROR(iov, "Unexpected CTXs reassignment: %u != %u\n",
			  num_ctxs, iov->vf.config.num_ctxs);
		return -EREMCHG;
	}
	if (iov->vf.config.num_dbs && iov->vf.config.num_dbs != num_dbs) {
		IOV_ERROR(iov, "Unexpected DBs reassignment: %u != %u\n",
			  num_dbs, iov->vf.config.num_dbs);
		return -EREMCHG;
	}

	iov->vf.config.num_ctxs = num_ctxs;
	iov->vf.config.num_dbs = num_dbs;

	return iov->vf.config.num_ctxs ? 0 : -ENODATA;
}

/**
 * intel_iov_query_config - Query IOV config data over MMIO.
 * @iov: the IOV struct
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_query_config(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = vf_get_ggtt_info(iov);
	if (unlikely(err))
		return err;

	err = vf_get_submission_cfg(iov);
	if (unlikely(err))
		return err;

	return 0;
}

static int iov_action_handshake(struct intel_iov *iov, u32 *major, u32 *minor)
{
	u32 request[VF2PF_HANDSHAKE_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_HANDSHAKE),
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, *major) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, *minor),
	};
	u32 response[VF2PF_HANDSHAKE_RESPONSE_MSG_LEN];
	int ret;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	ret = intel_iov_relay_send_to_pf(&iov->relay,
					 request, ARRAY_SIZE(request),
					 response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(ret != VF2PF_HANDSHAKE_RESPONSE_MSG_LEN))
		return -EPROTO;

	if (unlikely(FIELD_GET(VF2PF_HANDSHAKE_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	*major = FIELD_GET(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR, response[1]);
	*minor = FIELD_GET(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR, response[1]);

	return 0;
}

static int vf_handshake_with_pf(struct intel_iov *iov)
{
	u32 major_wanted = IOV_VERSION_LATEST_MAJOR;
	u32 minor_wanted = IOV_VERSION_LATEST_MINOR;
	u32 major = major_wanted, minor = minor_wanted;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = iov_action_handshake(iov, &major, &minor);
	if (unlikely(err))
		goto failed;

	IOV_DEBUG(iov, "Using ABI %u.%02u\n", major, minor);
	return 0;

failed:
	IOV_PROBE_ERROR(iov, "Unable to confirm ABI version %u.%02u (%pe)\n",
			major, minor, ERR_PTR(err));
	return err;
}

/**
 * intel_iov_query_version - Query IOV version info.
 * @iov: the IOV struct
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_query_version(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = vf_handshake_with_pf(iov);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	IOV_PROBE_ERROR(iov, "Failed to get version info (%pe)\n", ERR_PTR(err));
	return err;
}

static const i915_reg_t tgl_early_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN10_MIRROR_FUSE3,		/* _MMIO(0x9118) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	CTC_MODE,			/* _MMIO(0xA26C) */
};

static const i915_reg_t *get_early_regs(struct drm_i915_private *i915,
					unsigned int *size)
{
	const i915_reg_t *regs;

	if (IS_TIGERLAKE(i915) || IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915)) {
		regs = tgl_early_regs;
		*size = ARRAY_SIZE(tgl_early_regs);
	} else {
		MISSING_CASE(GRAPHICS_VER(i915));
		regs = ERR_PTR(-ENODEV);
		*size = 0;
	}

	return regs;
}

static void vf_cleanup_runtime_info(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	kfree(iov->vf.runtime.regs);
	iov->vf.runtime.regs = NULL;
	iov->vf.runtime.regs_size = 0;
}

static int vf_prepare_runtime_info(struct intel_iov *iov, unsigned int regs_size,
				   unsigned int alignment)
{
	unsigned int regs_size_up = roundup(regs_size, alignment);

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(iov->vf.runtime.regs_size && !iov->vf.runtime.regs);

	iov->vf.runtime.regs = krealloc(iov->vf.runtime.regs,
					regs_size_up * sizeof(struct vf_runtime_reg),
					__GFP_ZERO | GFP_NOWAIT | __GFP_NOWARN);
	if (unlikely(!iov->vf.runtime.regs))
		return -ENOMEM;

	iov->vf.runtime.regs_size = regs_size;

	return regs_size_up;
}

static void vf_show_runtime_info(struct intel_iov *iov)
{
	struct vf_runtime_reg *vf_regs = iov->vf.runtime.regs;
	unsigned int size = iov->vf.runtime.regs_size;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	for (; size--; vf_regs++) {
		IOV_DEBUG(iov, "RUNTIME reg[%#x] = %#x\n",
			  vf_regs->offset, vf_regs->value);
	}
}

static int guc_send_mmio_relay(struct intel_guc *guc, const u32 *request, u32 len,
			       u32 *response, u32 response_size)
{
	u32 magic1, magic2;
	int ret;

	GEM_BUG_ON(len < VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MIN_LEN);
	GEM_BUG_ON(response_size < VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MIN_LEN);

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, request[0]) != GUC_HXG_ORIGIN_HOST);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, request[0]) != GUC_HXG_TYPE_REQUEST);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, request[0]) !=
			     GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE);

	magic1 = FIELD_GET(VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC, request[0]);

	ret = intel_guc_send_mmio(guc, request, len, response, response_size);
	if (unlikely(ret < 0))
		return ret;

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, response[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, response[0]) != GUC_HXG_TYPE_RESPONSE_SUCCESS);

	magic2 = FIELD_GET(VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_0_MAGIC, response[0]);

	if (unlikely(magic1 != magic2))
		return -EPROTO;

	return ret;
}

static u32 mmio_relay_header(u32 opcode, u32 magic)
{
	return FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
	       FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
	       FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE) |
	       FIELD_PREP(VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC, magic) |
	       FIELD_PREP(VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_OPCODE, opcode);
}

static int vf_handshake_with_pf_mmio(struct intel_iov *iov)
{
	u32 major_wanted = IOV_VERSION_LATEST_MAJOR;
	u32 minor_wanted = IOV_VERSION_LATEST_MINOR;
	u32 request[VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MAX_LEN] = {
		mmio_relay_header(IOV_OPCODE_VF2PF_MMIO_HANDSHAKE, 0xF),
		FIELD_PREP(VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_1_MAJOR, major_wanted) |
		FIELD_PREP(VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_1_MINOR, minor_wanted),
	};
	u32 response[VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MAX_LEN];
	u32 major, minor;
	int ret;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	ret = guc_send_mmio_relay(iov_to_guc(iov), request, ARRAY_SIZE(request),
				  response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		goto failed;

	major = FIELD_GET(VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_1_MAJOR, response[1]);
	minor = FIELD_GET(VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_1_MINOR, response[1]);
	if (unlikely(major != major_wanted || minor != minor_wanted)) {
		ret = -ENOPKG;
		goto failed;
	}

	IOV_DEBUG(iov, "Using ABI %u.%02u\n", major, minor);
	return 0;

failed:
	IOV_PROBE_ERROR(iov, "Unable to confirm ABI version %u.%02u (%pe)\n",
			major_wanted, minor_wanted, ERR_PTR(ret));
	return -ECONNREFUSED;
}

static int vf_get_runtime_info_mmio(struct intel_iov *iov)
{
	u32 request[VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MAX_LEN];
	u32 response[VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MAX_LEN];
	u32 chunk = VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_NUM_OFFSET;
	unsigned int size, size_up, i, n;
	struct vf_runtime_reg *vf_regs;
	const i915_reg_t *regs;
	int ret;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	BUILD_BUG_ON(VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_NUM_OFFSET >
		     VF2PF_MMIO_GET_RUNTIME_RESPONSE_MSG_NUM_VALUE);

	regs = get_early_regs(iov_to_i915(iov), &size);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto failed;
	}
	if (!size)
		return 0;

	/*
	 * We want to allocate slightly larger buffer in order to align
	 * ourselves with GuC interface and avoid out-of-bounds write.
	 */
	ret = vf_prepare_runtime_info(iov, size, chunk);
	if (unlikely(ret < 0))
		goto failed;
	vf_regs = iov->vf.runtime.regs;
	size_up = ret;
	GEM_BUG_ON(!size_up);

	for (i = 0; i < size; i++)
		vf_regs[i].offset = i915_mmio_reg_offset(regs[i]);

	for (i = 0; i < size_up; i += chunk) {

		request[0] = mmio_relay_header(IOV_OPCODE_VF2PF_MMIO_GET_RUNTIME, 0);

		for (n = 0; n < chunk; n++)
			request[1 + n] = vf_regs[i + n].offset;

		/* we will use few bits from crc32 as magic */
		u32p_replace_bits(request, crc32_le(0, (void *)request, sizeof(request)),
				  VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC);

		ret = guc_send_mmio_relay(iov_to_guc(iov), request, ARRAY_SIZE(request),
					  response, ARRAY_SIZE(response));
		if (unlikely(ret < 0))
			goto failed;
		GEM_BUG_ON(ret != ARRAY_SIZE(response));

		for (n = 0; n < chunk; n++)
			vf_regs[i + n].value = response[1 + n];
	}

	return 0;

failed:
	vf_cleanup_runtime_info(iov);
	return ret;
}

static int vf_get_runtime_info_relay(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_i915(iov);
	u32 request[VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN];
	u32 response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MAX_LEN];
	u32 limit = (ARRAY_SIZE(response) - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / 2;
	u32 start = 0;
	u32 count, remaining, num, i;
	int ret;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(!limit);
	assert_rpm_wakelock_held(&i915->runtime_pm);

	request[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		     FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		     FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, IOV_ACTION_VF2PF_QUERY_RUNTIME) |
		     FIELD_PREP(VF2PF_QUERY_RUNTIME_REQUEST_MSG_0_LIMIT, limit);

repeat:
	request[1] = FIELD_PREP(VF2PF_QUERY_RUNTIME_REQUEST_MSG_1_START, start);
	ret = intel_iov_relay_send_to_pf(&iov->relay,
					 request, ARRAY_SIZE(request),
					 response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		goto failed;

	if (unlikely(ret < VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN)) {
		ret = -EPROTO;
		goto failed;
	}
	if (unlikely((ret - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) % 2)) {
		ret = -EPROTO;
		goto failed;
	}

	num = (ret - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / 2;
	count = FIELD_GET(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_0_COUNT, response[0]);
	remaining = FIELD_GET(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_1_REMAINING, response[1]);

	IOV_DEBUG(iov, "count=%u num=%u ret=%d start=%u remaining=%u\n",
		  count, num, ret, start, remaining);

	if (unlikely(count != num)) {
		ret = -EPROTO;
		goto failed;
	}

	if (start == 0) {
		ret = vf_prepare_runtime_info(iov, num + remaining, 1);
		if (unlikely(ret < 0))
			goto failed;
	} else if (unlikely(start + num > iov->vf.runtime.regs_size)) {
		ret = -EPROTO;
		goto failed;
	}

	for (i = 0; i < num; ++i) {
		struct vf_runtime_reg *reg = &iov->vf.runtime.regs[start + i];

		reg->offset = response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + 2 * i];
		reg->value = response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + 2 * i + 1];
	}

	if (remaining) {
		start += num;
		goto repeat;
	}

	return 0;

failed:
	vf_cleanup_runtime_info(iov);
	return ret;
}

/**
 * intel_iov_query_runtime - Query IOV runtime data.
 * @iov: the IOV struct
 * @early: use early MMIO access
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_query_runtime(struct intel_iov *iov, bool early)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	if (early) {
		err = vf_handshake_with_pf_mmio(iov);
		if (unlikely(err))
			goto failed;
	}

	if (early)
		err = vf_get_runtime_info_mmio(iov);
	else
		err = vf_get_runtime_info_relay(iov);
	if (unlikely(err))
		goto failed;

	vf_show_runtime_info(iov);
	return 0;

failed:
	IOV_PROBE_ERROR(iov, "Failed to get runtime info (%pe)\n",
			ERR_PTR(err));
	return err;
}

/**
 * intel_iov_query_fini - Cleanup all queried IOV data.
 * @iov: the IOV struct
 *
 * This function is for VF use only.
 */
void intel_iov_query_fini(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	vf_cleanup_runtime_info(iov);
}

/**
 * intel_iov_query_print_config - Print queried VF config.
 * @iov: the IOV struct
 * @p: the DRM printer
 *
 * This function is for VF use only.
 */
void intel_iov_query_print_config(struct intel_iov *iov, struct drm_printer *p)
{
	GEM_BUG_ON(!intel_iov_is_vf(iov));

	drm_printf(p, "GGTT range:\t%#08llx-%#08llx\n",
			iov->vf.config.ggtt_base,
			iov->vf.config.ggtt_base + iov->vf.config.ggtt_size - 1);
	drm_printf(p, "GGTT size:\t%lluK\n", iov->vf.config.ggtt_size / SZ_1K);

	drm_printf(p, "contexts:\t%hu\n", iov->vf.config.num_ctxs);
	drm_printf(p, "doorbells:\t%hu\n", iov->vf.config.num_dbs);
}
