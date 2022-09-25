// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_provisioning.h"
#include "intel_iov_query.h"
#include "intel_iov_relay.h"
#include "intel_iov_service.h"
#include "intel_iov_state.h"
#include "intel_iov_utils.h"
#include "gt/intel_gt_pm.h"

/**
 * intel_iov_init_early - Prepare IOV data.
 * @iov: the IOV struct
 *
 * Early initialization of the I/O Virtualization data.
 */
void intel_iov_init_early(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov)) {
		intel_iov_provisioning_init_early(iov);
		intel_iov_service_init_early(iov);
		intel_iov_state_init_early(iov);
	}

	intel_iov_relay_init_early(&iov->relay);
}

/**
 * intel_iov_release - Release IOV data.
 * @iov: the IOV struct
 *
 * This function will release any data prepared in @intel_iov_init_early.
 */
void intel_iov_release(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov)) {
		intel_iov_state_release(iov);
		intel_iov_service_release(iov);
		intel_iov_provisioning_release(iov);
	}
}

/**
 * intel_iov_init_mmio - Initialize IOV based on MMIO data.
 * @iov: the IOV struct
 *
 * On VF this function will read SR-IOV INIT message from GuC.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_mmio(struct intel_iov *iov)
{
	int ret;

	if (intel_iov_is_vf(iov)) {
		ret = intel_iov_query_bootstrap(iov);
		if (unlikely(ret))
			return ret;
		ret = intel_iov_query_config(iov);
		if (unlikely(ret))
			return ret;
		ret = intel_iov_query_runtime(iov, true);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

static int vf_tweak_guc_submission(struct intel_iov *iov)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	err = intel_guc_submission_limit_ids(iov_to_guc(iov),
					     iov->vf.config.num_ctxs);
	if (unlikely(err))
		IOV_ERROR(iov, "Failed to limit %s to %u (%pe)\n",
			  "contexts", iov->vf.config.num_ctxs, ERR_PTR(err));

	return err;
}

/**
 * intel_iov_init - Initialize IOV.
 * @iov: the IOV struct
 *
 * On PF this function performs initial partitioning of the shared resources
 * that can't be changed later (GuC submission contexts) to allow early PF
 * provisioning.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov))
		intel_iov_provisioning_init(iov);

	if (intel_iov_is_vf(iov))
		vf_tweak_guc_submission(iov);

	return 0;
}

/**
 * intel_iov_fini - Cleanup IOV.
 * @iov: the IOV struct
 *
 * This function will cleanup any data prepared in @intel_iov_init.
 */
void intel_iov_fini(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov))
		intel_iov_provisioning_fini(iov);
}

static int vf_balloon_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;
	u64 start, end;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	/*
	 * We can only use part of the GGTT as allocated by PF.
	 *
	 *      0                                      GUC_GGTT_TOP
	 *      |<------------ Total GGTT size ------------------>|
	 *
	 *      |<-- VF GGTT base -->|<- size ->|
	 *
	 *      +--------------------+----------+-----------------+
	 *      |////////////////////|   block  |\\\\\\\\\\\\\\\\\|
	 *      +--------------------+----------+-----------------+
	 *
	 *      |<--- balloon[0] --->|<-- VF -->|<-- balloon[1] ->|
	 */

	start = 0;
	end = iov->vf.config.ggtt_base;
	err = i915_ggtt_balloon(ggtt, start, end, &iov->vf.ggtt_balloon[0]);
	if (unlikely(err))
		return err;

	start = iov->vf.config.ggtt_base + iov->vf.config.ggtt_size;
	end = GUC_GGTT_TOP;
	err = i915_ggtt_balloon(ggtt, start, end, &iov->vf.ggtt_balloon[1]);

	return err;
}

static void vf_deballoon_ggtt(struct intel_iov *iov)
{
	struct i915_ggtt *ggtt = iov_to_gt(iov)->ggtt;

	i915_ggtt_deballoon(ggtt, &iov->vf.ggtt_balloon[1]);
	i915_ggtt_deballoon(ggtt, &iov->vf.ggtt_balloon[0]);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
static int igt_vf_iov_own_ggtt(struct intel_iov *iov, bool sanitycheck);
#endif

/**
 * intel_iov_init_ggtt - Initialize GGTT for SR-IOV.
 * @iov: the IOV struct
 *
 * On the VF this function will balloon GGTT to make sure only assigned region
 * will be used for allocations.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_ggtt(struct intel_iov *iov)
{
	int err;

	if (intel_iov_is_vf(iov)) {
		err = vf_balloon_ggtt(iov);
		if (unlikely(err))
			return err;
#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
		igt_vf_iov_own_ggtt(iov, true);
#endif
	}

	return 0;
}

/**
 * intel_iov_fini_ggtt - Cleanup SR-IOV hardware support.
 * @iov: the IOV struct
 */
void intel_iov_fini_ggtt(struct intel_iov *iov)
{
	if (intel_iov_is_vf(iov))
		vf_deballoon_ggtt(iov);
}

static void pf_enable_ggtt_guest_update(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);

	/* Guest Direct GGTT Update Enable */
	intel_uncore_write(gt->uncore, GEN12_VIRTUAL_CTRL_REG,
			   GEN12_GUEST_GTT_UPDATE_EN);
}

/**
 * intel_iov_init_hw - Initialize SR-IOV hardware support.
 * @iov: the IOV struct
 *
 * PF must configure hardware to enable VF's access to GGTT.
 * PF also updates here runtime info (snapshot of registers values)
 * that will be shared with VFs.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_hw(struct intel_iov *iov)
{
	int err;

	if (intel_iov_is_pf(iov)) {
		pf_enable_ggtt_guest_update(iov);
		intel_iov_service_update(iov);
		intel_iov_provisioning_restart(iov);
		intel_iov_state_reset(iov);
	}

	if (intel_iov_is_vf(iov)) {
		err = intel_iov_query_runtime(iov, false);
		if (unlikely(err))
			return -EIO;
	}

	return 0;
}

/**
 * intel_iov_fini_hw - Cleanup data initialized in iov_init_hw.
 * @iov: the IOV struct
 */
void intel_iov_fini_hw(struct intel_iov *iov)
{
	if (intel_iov_is_pf(iov))
		intel_iov_service_reset(iov);

	if (intel_iov_is_vf(iov))
		intel_iov_query_fini(iov);
}

/**
 * intel_iov_init_late - Late initialization of SR-IOV support.
 * @iov: the IOV struct
 *
 * This function continues necessary initialization of the SR-IOV
 * support in the driver and the hardware.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_init_late(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);

	if (intel_iov_is_pf(iov)) {
		/*
		 * GuC submission must be working on PF to allow VFs to work.
		 * If unavailable, mark as PF error, but it's safe to continue.
		 */
		if (unlikely(!intel_uc_uses_guc_submission(&gt->uc))) {
			pf_update_status(iov, -EIO, "GuC");
			return 0;
		}
	}

	if (intel_iov_is_vf(iov)) {
		/*
		 * If we try to start VF driver without GuC submission enabled,
		 * then use -EIO error to keep driver alive but without GEM.
		 */
		if (!intel_uc_uses_guc_submission(&gt->uc)) {
			dev_warn(gt->i915->drm.dev, "GuC submission is %s\n",
				 str_enabled_disabled(false));
			return -EIO;
		}
	}

	return 0;
}

void intel_iov_pf_get_pm_vfs(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	intel_gt_pm_get_untracked(iov_to_gt(iov));
}

void intel_iov_pf_put_pm_vfs(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	intel_gt_pm_put_untracked(iov_to_gt(iov));
}

void intel_iov_suspend(struct intel_iov *iov)
{
	if (!intel_iov_is_pf(iov))
		return;

	if (pci_num_vf(to_pci_dev(iov_to_i915(iov)->drm.dev)) != 0)
		intel_iov_pf_put_pm_vfs(iov);
}

void intel_iov_resume(struct intel_iov *iov)
{
	if (!intel_iov_is_pf(iov))
		return;

	if (pci_num_vf(to_pci_dev(iov_to_i915(iov)->drm.dev)) != 0)
		intel_iov_pf_get_pm_vfs(iov);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/selftest_live_iov_ggtt.c"
#endif
