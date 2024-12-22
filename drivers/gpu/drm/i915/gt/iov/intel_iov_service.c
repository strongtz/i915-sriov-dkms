// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bsearch.h>

#include "abi/iov_actions_abi.h"
#include "abi/iov_actions_mmio_abi.h"
#include "abi/iov_errors_abi.h"
#include "abi/iov_messages_abi.h"
#include "abi/iov_version_abi.h"

#include "gt/intel_gt_regs.h"

#include "intel_iov_ggtt.h"
#include "intel_iov_relay.h"
#include "intel_iov_service.h"
#include "intel_iov_types.h"
#include "intel_iov_utils.h"

static void __uncore_read_many(struct intel_uncore *uncore, unsigned int count,
			       const i915_reg_t *regs, u32 *values)
{
	while (count--) {
		*values++ = intel_uncore_read(uncore, *regs++);
	}
}

static const i915_reg_t tgl_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	GEN10_MIRROR_FUSE3,		/* _MMIO(0x9118) */
	GEN11_EU_DISABLE,		/* _MMIO(0x9134) */
	GEN11_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	GEN12_GT_COMPUTE_DSS_ENABLE,    /* _MMIO(0x9144) */
	CTC_MODE,			/* _MMIO(0xA26C) */
	GEN11_HUC_KERNEL_LOAD_INFO,	/* _MMIO(0xC1DC) */
	GEN9_TIMESTAMP_OVERRIDE,	/* _MMIO(0x44074) */
};

static const i915_reg_t mtl_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0D00) */
	XEHP_FUSE4,			/* _MMIO(0x9114) */
	GEN10_MIRROR_FUSE3,		/* _MMIO(0x9118) */
	HSW_PAVP_FUSE1,			/* _MMIO(0x911C) */
	XEHP_EU_ENABLE,			/* _MMIO(0x9134) */
	GEN12_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913C) */
	GEN11_GT_VEBOX_VDBOX_DISABLE,	/* _MMIO(0x9140) */
	GEN12_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	XEHPC_GT_COMPUTE_DSS_ENABLE_EXT,/* _MMIO(0x9148) */
	CTC_MODE,			/* _MMIO(0xA26C) */
	GEN11_HUC_KERNEL_LOAD_INFO,	/* _MMIO(0xC1DC) */
	GEN9_TIMESTAMP_OVERRIDE,	/* _MMIO(0x44074) */
	_MMIO(0x10100C),
	_MMIO(MTL_GSC_HECI1_BASE + HECI_FWSTS5),/* _MMIO(0x116c68) */
	MTL_GT_ACTIVITY_FACTOR,		/* _MMIO(0x138010) */
	_MMIO(0x389140),
	_MMIO(0x38C1DC),                /* _MMIO(0x38C1DC) */
};

static const i915_reg_t *get_runtime_regs(struct drm_i915_private *i915,
					  unsigned int *size)
{
	const i915_reg_t *regs;

	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70)) {
		regs = mtl_runtime_regs;
		*size = ARRAY_SIZE(mtl_runtime_regs);
	} else if (IS_TIGERLAKE(i915) || IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915))  {
		regs = tgl_runtime_regs;
		*size = ARRAY_SIZE(tgl_runtime_regs);
	} else {
		MISSING_CASE(GRAPHICS_VER(i915));
		regs = ERR_PTR(-ENODEV);
		*size = 0;
	}

	return regs;
}

static bool regs_selftest(const i915_reg_t *regs, unsigned int count)
{
	u32 offset = 0;

	while (IS_ENABLED(CONFIG_DRM_I915_SELFTEST) && count--) {
		if (i915_mmio_reg_offset(*regs) < offset) {
			pr_err("invalid register order: %#x < %#x\n",
				i915_mmio_reg_offset(*regs), offset);
			return false;
		}
		offset = i915_mmio_reg_offset(*regs++);
	}

	return true;
}

static int pf_alloc_runtime_info(struct intel_iov *iov)
{
	const i915_reg_t *regs;
	unsigned int size;
	u32 *values;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(iov->pf.service.runtime.size);
	GEM_BUG_ON(iov->pf.service.runtime.regs);
	GEM_BUG_ON(iov->pf.service.runtime.values);

	regs = get_runtime_regs(iov_to_i915(iov), &size);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	if (unlikely(!size))
		return 0;

	if (unlikely(!regs_selftest(regs, size)))
		return -EBADSLT;

	values = kcalloc(size, sizeof(u32), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	iov->pf.service.runtime.size = size;
	iov->pf.service.runtime.regs = regs;
	iov->pf.service.runtime.values = values;

	return 0;
}

static void pf_release_runtime_info(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	kfree(iov->pf.service.runtime.values);
	iov->pf.service.runtime.values = NULL;
	iov->pf.service.runtime.regs = NULL;
	iov->pf.service.runtime.size = 0;
}

static void pf_prepare_runtime_info(struct intel_iov *iov)
{
	const i915_reg_t *regs;
	unsigned int size;
	u32 *values;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.service.runtime.size)
		return;

	size = iov->pf.service.runtime.size;
	regs = iov->pf.service.runtime.regs;
	values = iov->pf.service.runtime.values;

	__uncore_read_many(iov_to_gt(iov)->uncore, size, regs, values);

	while (size--) {
		IOV_DEBUG(iov, "reg[%#x] = %#x\n",
			  i915_mmio_reg_offset(*regs++), *values++);
	}
}

static void pf_reset_runtime_info(struct intel_iov *iov)
{
	unsigned int size;
	u32 *values;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.service.runtime.size)
		return;

	size = iov->pf.service.runtime.size;
	values = iov->pf.service.runtime.values;

	while (size--)
		*values++ = 0;
}

static bool vfpf_update_ggtt_is_supported(struct intel_iov *iov)
{
	return MEDIA_VER_FULL(iov_to_i915(iov)) == IP_VER(13, 0);
}

/**
 * intel_iov_service_init_early - Early initialization of the PF IOV services.
 * @iov: the IOV struct
 *
 * Performs early initialization of the IOV PF services, including preparation
 * of the runtime info that will be shared with VFs.
 *
 * This function can only be called on PF.
 */
void intel_iov_service_init_early(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	err = pf_alloc_runtime_info(iov);
	if (unlikely(err))
		pf_update_status(iov, err, "runtime");
}

/**
 * intel_iov_service_release - Cleanup PF IOV services.
 * @iov: the IOV struct
 *
 * Releases any data allocated during initialization.
 *
 * This function can only be called on PF.
 */
void intel_iov_service_release(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	pf_release_runtime_info(iov);
}

/**
 * intel_iov_service_update - Update PF IOV services.
 * @iov: the IOV struct
 *
 * Updates runtime data shared with VFs.
 *
 * This function can be called more than once.
 * This function can only be called on PF.
 */
void intel_iov_service_update(struct intel_iov *iov)
{
	struct intel_gt *media_gt = iov_to_i915(iov)->media_gt;
	enum forcewake_domains fw;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (media_gt) {
		fw = intel_uncore_forcewake_for_reg(media_gt->uncore, _MMIO(0x38c1dc), FW_REG_READ);
		intel_uncore_forcewake_get(media_gt->uncore, fw);
	}

	pf_prepare_runtime_info(iov);

	if (media_gt)
		intel_uncore_forcewake_put(media_gt->uncore, fw);
}

/**
 * intel_iov_service_reset - Update PF IOV services.
 * @iov: the IOV struct
 *
 * Resets runtime data to avoid sharing stale info with VFs.
 *
 * This function can be called more than once.
 * This function can only be called on PF.
 */
void intel_iov_service_reset(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	pf_reset_runtime_info(iov);
}

static int reply_handshake(struct intel_iov *iov, u32 origin,
			   u32 relay_id, const u32 *msg, u32 len)
{
	struct intel_iov_relay *relay = &iov->relay;
	u32 response[VF2PF_HANDSHAKE_RESPONSE_MSG_LEN];
	u32 wanted_major, wanted_minor;
	u32 major, minor, mbz;

	GEM_BUG_ON(!origin &&
		   !I915_SELFTEST_ONLY(relay->selftest.enable_loopback));

	if (unlikely(len != VF2PF_HANDSHAKE_REQUEST_MSG_LEN))
		return -EMSGSIZE;

	wanted_major = FIELD_GET(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, msg[1]);
	wanted_minor = FIELD_GET(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, msg[1]);
	IOV_DEBUG(iov, "VF%u wants ABI version %u.%02u\n", origin,
		  wanted_major, wanted_minor);

	mbz = FIELD_GET(VF2PF_HANDSHAKE_REQUEST_MSG_0_MBZ, msg[0]);
	if (unlikely(mbz))
		return -EINVAL;

	if (!wanted_major && !wanted_minor) {
		major = IOV_VERSION_LATEST_MAJOR;
		minor = IOV_VERSION_LATEST_MINOR;
	} else if (wanted_major > IOV_VERSION_LATEST_MAJOR) {
		major = IOV_VERSION_LATEST_MAJOR;
		minor = IOV_VERSION_LATEST_MINOR;
	} else if (wanted_major < IOV_VERSION_BASE_MAJOR) {
		return -EINVAL;
	} else if (wanted_major < IOV_VERSION_LATEST_MAJOR) {
		major = wanted_major;
		minor = wanted_minor;
	} else {
		major = wanted_major;
		minor = min_t(u32, IOV_VERSION_LATEST_MINOR, wanted_minor);
	}

	response[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		      FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		      FIELD_PREP(GUC_HXG_RESPONSE_MSG_0_DATA0, 0);

	response[1] = FIELD_PREP(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR, major) |
		      FIELD_PREP(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR, minor);
	return intel_iov_relay_reply_to_vf(relay, origin, relay_id,
					   response, ARRAY_SIZE(response));
}

static int pf_reply_runtime_query(struct intel_iov *iov, u32 origin,
				  u32 relay_id, const u32 *msg, u32 len)
{
	struct intel_iov_runtime_regs *runtime = &iov->pf.service.runtime;
	u32 response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MAX_LEN];
	u32 max_chunk = (ARRAY_SIZE(response) - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / 2;
	u32 limit, start, chunk, i;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (unlikely(len > VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN))
		return -EMSGSIZE;
	if (unlikely(len < VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN))
		return -EPROTO;

	limit = FIELD_GET(VF2PF_QUERY_RUNTIME_REQUEST_MSG_0_LIMIT, msg[0]);
	start = FIELD_GET(VF2PF_QUERY_RUNTIME_REQUEST_MSG_1_START, msg[1]);
	if (unlikely(start > runtime->size))
		return -EINVAL;

	chunk = min_t(u32, runtime->size - start, max_chunk);
	if (limit)
		chunk = min_t(u32, chunk, limit);

	response[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		      FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		      FIELD_PREP(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_0_COUNT, chunk);

	response[1] = FIELD_PREP(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_1_REMAINING,
				 runtime->size - start - chunk);

	for (i = 0; i < chunk; ++i) {
		i915_reg_t reg = runtime->regs[start + i];
		u32 offset = i915_mmio_reg_offset(reg);
		u32 value = runtime->values[start + i];

		response[2 + 2 * i] = offset;
		response[2 + 2 * i + 1] = value;
	}

	return intel_iov_relay_reply_to_vf(&iov->relay, origin, relay_id,
					   response, 2 + 2 * chunk);
}

static gen8_pte_t get_pte_from_msg(const u32 *msg, u16 id)
{
	u32 pte_lo = FIELD_GET(VF2PF_UPDATE_GGTT32_REQUEST_DATAn_PTE_LO, msg[id * 2 + 2]);
	u32 pte_hi = FIELD_GET(VF2PF_UPDATE_GGTT32_REQUEST_DATAn_PTE_HI, msg[id * 2 + 3]);

	return make_u64(pte_hi, pte_lo);
}

static int pf_reply_update_ggtt(struct intel_iov *iov, u32 origin,
				u32 relay_id, const u32 *msg, u32 len)
{
	u32 response[VF2PF_UPDATE_GGTT32_RESPONSE_MSG_LEN];
	u16 num_copies;
	u8 mode;
	u32 pte_offset;
	u16 count;
	gen8_pte_t ptes[VF2PF_UPDATE_GGTT_MAX_PTES];
	gen8_pte_t *start_range;
	u16 range_size;
	u16 updated = 0;
	int ret;
	int i;

	if (!vfpf_update_ggtt_is_supported(iov))
		return -EOPNOTSUPP;

	if (unlikely(!msg[0]) || unlikely(!msg[1]) || len < 4 || len % 2 != 0)
		return -EPROTO;

	num_copies = FIELD_GET(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_NUM_COPIES, msg[1]);
	mode = FIELD_GET(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_MODE, msg[1]);
	pte_offset = FIELD_GET(VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_OFFSET, msg[1]);
	count = (len - 2) / 2;

	if (count > VF2PF_UPDATE_GGTT_MAX_PTES)
		return -EMSGSIZE;

	ptes[0] = get_pte_from_msg(msg, 0);
	start_range = &ptes[0];
	range_size = 1;

	for (i = 1; i < count; i++) {
		ptes[i] = get_pte_from_msg(msg, i);

		if ((ptes[i - 1] & ~GEN12_GGTT_PTE_ADDR_MASK) !=
		    (ptes[i] & ~GEN12_GGTT_PTE_ADDR_MASK)) {
			u16 local_num_copies = (VF2PF_UPDATE_GGTT32_IS_LAST_MODE(mode)) ?
					       0 : num_copies;

			ret = intel_iov_ggtt_pf_update_vf_ptes(iov, origin, pte_offset, mode,
							       local_num_copies, start_range, range_size);
			if (ret < 0) {
				return ret;
			} else {
				updated += ret;
				start_range = &ptes[i];
				pte_offset += range_size;
				range_size = 0;
				if (!VF2PF_UPDATE_GGTT32_IS_LAST_MODE(mode))
					num_copies = 0;
			}
		}
		range_size++;
	}

	ret = intel_iov_ggtt_pf_update_vf_ptes(iov, origin, pte_offset, mode, num_copies,
					       start_range, range_size);
	if (ret < 0)
		return ret;

	updated += ret;

	response[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		      FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		      FIELD_PREP(VF2PF_UPDATE_GGTT32_RESPONSE_MSG_0_NUM_PTES, updated);

	return intel_iov_relay_reply_to_vf(&iov->relay, origin, relay_id,
					   response, ARRAY_SIZE(response));
}

/**
 * intel_iov_service_process_msg - Service request message from VF.
 * @iov: the IOV struct
 * @origin: origin VF number
 * @relay_id: message ID
 * @msg: request message
 * @len: length of the message (in dwords)
 *
 * This function processes `IOV Message`_ from the VF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_service_process_msg(struct intel_iov *iov, u32 origin,
				  u32 relay_id, const u32 *msg, u32 len)
{
	int err = -EOPNOTSUPP;
	u32 action;
	u32 __maybe_unused data;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(len < GUC_HXG_MSG_MIN_LEN);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_REQUEST);

	action = FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]);
	data = FIELD_GET(GUC_HXG_REQUEST_MSG_0_DATA0, msg[0]);
	IOV_DEBUG(iov, "servicing action %#x:%u from %u\n", action, data, origin);

	if (!origin && !I915_SELFTEST_ONLY(iov->relay.selftest.enable_loopback))
		return -EPROTO;

	switch (action) {
	case IOV_ACTION_VF2PF_HANDSHAKE:
		err = reply_handshake(iov, origin, relay_id, msg, len);
		break;
	case IOV_ACTION_VF2PF_QUERY_RUNTIME:
		err = pf_reply_runtime_query(iov, origin, relay_id, msg, len);
		break;
	case IOV_ACTION_VF2PF_UPDATE_GGTT32:
		err = pf_reply_update_ggtt(iov, origin, relay_id, msg, len);
		break;
	default:
		break;
	}

	return err;
}

static int send_mmio_relay_error(struct intel_iov *iov,
				 u32 vfid, u32 magic, int fault)
{
	u32 request[PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_MMIO_RELAY_FAILURE),
		FIELD_PREP(PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_1_VFID, vfid),
		FIELD_PREP(PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_2_MAGIC, magic) |
		FIELD_PREP(PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_2_FAULT, fault),
	};

	return intel_guc_send(iov_to_guc(iov), request, ARRAY_SIZE(request));
}

static int send_mmio_relay_reply(struct intel_iov *iov,
				 u32 vfid, u32 magic, u32 data[4])
{
	u32 request[PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_MMIO_RELAY_SUCCESS),
		FIELD_PREP(PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_1_VFID, vfid),
		FIELD_PREP(PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_2_MAGIC, magic) |
		FIELD_PREP(PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_2_DATA0, data[0]),
		FIELD_PREP(PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_n_DATAx, data[1]),
		FIELD_PREP(PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_n_DATAx, data[2]),
		FIELD_PREP(PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_n_DATAx, data[3]),
	};

	return intel_guc_send(iov_to_guc(iov), request, ARRAY_SIZE(request));
}

static int reply_mmio_relay_handshake(struct intel_iov *iov,
				      u32 vfid, u32 magic, const u32 *msg)
{
	u32 data[PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_NUM_DATA + 1] = { };
	u32 wanted_major = FIELD_GET(VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_1_MAJOR, msg[1]);
	u32 wanted_minor = FIELD_GET(VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_1_MINOR, msg[1]);
	u32 major = 0, minor = 0;
	int fault = 0;

	IOV_DEBUG(iov, "VF%u wants ABI version %u.%02u\n", vfid, wanted_major, wanted_minor);

	/* XXX for now we only support single major version (latest) */

	if (!wanted_major && !wanted_minor) {
		major = IOV_VERSION_LATEST_MAJOR;
		minor = IOV_VERSION_LATEST_MINOR;
	} else if (wanted_major > IOV_VERSION_LATEST_MAJOR) {
		major = IOV_VERSION_LATEST_MAJOR;
		minor = IOV_VERSION_LATEST_MINOR;
	} else if (wanted_major < IOV_VERSION_LATEST_MAJOR) {
		fault = ENOPKG;
	} else {
		GEM_BUG_ON(wanted_major != IOV_VERSION_LATEST_MAJOR);
		GEM_BUG_ON(IOV_VERSION_LATEST_MAJOR != 1);

		if (unlikely(!msg[0] || msg[2] || msg[3])) {
			fault = EPROTO;
		} else {
			major = wanted_major;
			minor = min_t(u32, IOV_VERSION_LATEST_MINOR, wanted_minor);
		}
	}

	if (fault)
		return send_mmio_relay_error(iov, vfid, magic, fault);

	IOV_DEBUG(iov, "VF%u will use ABI version %u.%02u\n", vfid, major, minor);

	data[1] = FIELD_PREP(VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_1_MAJOR, major) |
		  FIELD_PREP(VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_1_MINOR, minor);

	return send_mmio_relay_reply(iov, vfid, magic, data);
}

static int reply_mmio_relay_update_ggtt(struct intel_iov *iov, u32 vfid, u32 magic, const u32 *msg)
{
	u32 data[PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_NUM_DATA + 1] = { };
	u16 num_copies;
	u8 mode;
	u32 pte_lo, pte_hi;
	u32 pte_offset;
	gen8_pte_t pte;
	int ret;

	if (!vfpf_update_ggtt_is_supported(iov))
		return -EOPNOTSUPP;

	if (unlikely(!msg[0]))
		return -EPROTO;

	num_copies = FIELD_GET(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES, msg[1]);
	mode = FIELD_GET(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_MODE, msg[1]);
	pte_offset = FIELD_GET(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_OFFSET, msg[1]);
	pte_lo = FIELD_GET(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_2_PTE_LO, msg[2]);
	pte_hi = FIELD_GET(VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_3_PTE_HI, msg[3]);

	pte = make_u64(pte_hi, pte_lo);

	ret = intel_iov_ggtt_pf_update_vf_ptes(iov, vfid, pte_offset, mode, num_copies, &pte, 1);
	if (ret < 0)
		return ret;
	data[0] = FIELD_PREP(VF2PF_MMIO_UPDATE_GGTT_RESPONSE_MSG_1_NUM_PTES, (u16)ret);

	return send_mmio_relay_reply(iov, vfid, magic, data);
}

static int __i915_reg_cmp(const void *a, const void *b)
{
	return i915_mmio_reg_offset(*(const i915_reg_t *)a) -
	       i915_mmio_reg_offset(*(const i915_reg_t *)b);
}

static int lookup_reg_index(struct intel_iov *iov, u32 offset)
{
	i915_reg_t key = _MMIO(offset);
	i915_reg_t *found = bsearch(&key, iov->pf.service.runtime.regs,
				    iov->pf.service.runtime.size, sizeof(key),
				    __i915_reg_cmp);

	return found ? found - iov->pf.service.runtime.regs : -ENODATA;
}

static int reply_mmio_relay_get_reg(struct intel_iov *iov,
				    u32 vfid, u32 magic, const u32 *msg)
{
	u32 data[PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_NUM_DATA + 1] = { };
	unsigned int i;
	int found;

	BUILD_BUG_ON(VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_NUM_OFFSET >
		     GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_NUM_DATA);
	BUILD_BUG_ON(VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_NUM_OFFSET !=
		     PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_NUM_DATA);

	if (unlikely(!msg[0]))
		return -EPROTO;
	if (unlikely(!msg[1]))
		return -EINVAL;

	for (i = 0; i < VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_NUM_OFFSET; i++) {
		u32 offset = msg[i + 1];

		if (unlikely(!offset))
			continue;
		found = lookup_reg_index(iov, offset);
		if (found < 0)
			return -EACCES;
		data[i + 1] = iov->pf.service.runtime.values[found];
	}

	return send_mmio_relay_reply(iov, vfid, magic, data);
}

/**
 * intel_iov_service_process_mmio_relay - Process MMIO Relay notification.
 * @iov: the IOV struct
 * @msg: mmio relay notification data
 * @len: length of the message data (in dwords)
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_service_process_mmio_relay(struct intel_iov *iov, const u32 *msg,
					 u32 len)
{
	struct drm_i915_private *i915 = iov_to_i915(iov);
	struct intel_runtime_pm *rpm = &i915->runtime_pm;
	intel_wakeref_t wakeref;
	u32 vfid, magic, opcode;
	int err = -EPROTO;

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) !=
		   GUC_ACTION_GUC2PF_MMIO_RELAY_SERVICE);

	if (unlikely(!IS_SRIOV_PF(i915)))
		return -EPERM;
	if (unlikely(len != GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_LEN))
		return -EPROTO;

	vfid = FIELD_GET(GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_1_VFID, msg[1]);
	magic = FIELD_GET(GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_2_MAGIC, msg[2]);
	opcode = FIELD_GET(GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_2_OPCODE, msg[2]);

	if (unlikely(!vfid))
		return -EPROTO;

	wakeref = intel_runtime_pm_get(rpm);

	switch (opcode) {
	case IOV_OPCODE_VF2PF_MMIO_HANDSHAKE:
		err = reply_mmio_relay_handshake(iov, vfid, magic, msg + 2);
		break;
	case IOV_OPCODE_VF2PF_MMIO_UPDATE_GGTT:
		err = reply_mmio_relay_update_ggtt(iov, vfid, magic, msg + 2);
		break;
	case IOV_OPCODE_VF2PF_MMIO_GET_RUNTIME:
		err = reply_mmio_relay_get_reg(iov, vfid, magic, msg + 2);
		break;
	default:
		IOV_DEBUG(iov, "unsupported request %#x from VF%u\n",
				opcode, vfid);
		err = -EOPNOTSUPP;
	}

	if (unlikely(err < 0))
		send_mmio_relay_error(iov, vfid, magic, -err);

	intel_runtime_pm_put(rpm, wakeref);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/selftest_mock_iov_service.c"
#include "selftests/selftest_live_iov_service.c"
#endif
