// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_sriov.h"
#include "i915_sriov_sysfs.h"
#include "i915_drv.h"
#include "i915_pci.h"
#include "i915_reg.h"
#include "intel_pci_config.h"

#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/iov/intel_iov_provisioning.h"
#include "gt/iov/intel_iov_state.h"
#include "gt/iov/intel_iov_utils.h"

/* safe for use before register access via uncore is completed */
static u32 pci_peek_mmio_read32(struct pci_dev *pdev, i915_reg_t reg)
{
	unsigned long offset = i915_mmio_reg_offset(reg);
	void __iomem *addr;
	u32 value;

	addr = pci_iomap_range(pdev, 0, offset, sizeof(u32));
	if (WARN(!addr, "Failed to map MMIO at %#lx\n", offset))
		return 0;

	value = readl(addr);
	pci_iounmap(pdev, addr);

	return value;
}

static bool gen12_pci_capability_is_vf(struct pci_dev *pdev)
{
	u32 value = pci_peek_mmio_read32(pdev, GEN12_VF_CAP_REG);

	/*
	 * Bugs in PCI programming (or failing hardware) can occasionally cause
	 * lost access to the MMIO BAR.  When this happens, register reads will
	 * come back with 0xFFFFFFFF for every register, including VF_CAP, and
	 * then we may wrongly claim that we are running on the VF device.
	 * Since VF_CAP has only one bit valid, make sure no other bits are set.
	 */
	if (WARN(value & ~GEN12_VF, "MMIO BAR malfunction, %#x returned %#x\n",
		 i915_mmio_reg_offset(GEN12_VF_CAP_REG), value))
		return false;

	return value & GEN12_VF;
}

#ifdef CONFIG_PCI_IOV

static unsigned int wanted_max_vfs(struct drm_i915_private *i915)
{
	return i915->params.max_vfs;
}

static int pf_reduce_totalvfs(struct drm_i915_private *i915, int limit)
{
	int err;

	err = pci_sriov_set_totalvfs(to_pci_dev(i915->drm.dev), limit);
	drm_WARN(&i915->drm, err, "Failed to set number of VFs to %d (%pe)\n",
		 limit, ERR_PTR(err));
	return err;
}

static bool pf_has_valid_vf_bars(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	if (!i915_pci_resource_valid(pdev, GEN12_VF_GTTMMADR_BAR))
		return false;

	if (HAS_LMEM(i915) && !i915_pci_resource_valid(pdev, GEN12_VF_LMEM_BAR))
		return false;

	return true;
}

static bool pf_continue_as_native(struct drm_i915_private *i915, const char *why)
{
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
	drm_dbg(&i915->drm, "PF: %s, continuing as native\n", why);
#endif
	pf_reduce_totalvfs(i915, 0);
	return false;
}

static bool pf_verify_readiness(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int totalvfs = pci_sriov_get_totalvfs(pdev);
	int newlimit = min_t(u16, wanted_max_vfs(i915), totalvfs);

	GEM_BUG_ON(!dev_is_pf(dev));
	GEM_WARN_ON(totalvfs > U16_MAX);

	if (!newlimit)
		return pf_continue_as_native(i915, "all VFs disabled");

	if (!pf_has_valid_vf_bars(i915))
		return pf_continue_as_native(i915, "VFs BAR not ready");

	pf_reduce_totalvfs(i915, newlimit);

	i915->sriov.pf.device_vfs = totalvfs;
	i915->sriov.pf.driver_vfs = newlimit;

	return true;
}

#else

static int pf_reduce_totalvfs(struct drm_i915_private *i915, int limit)
{
	return 0;
}

#endif

/**
 * i915_sriov_probe - Probe I/O Virtualization mode.
 * @i915: the i915 struct
 *
 * This function should be called once and as soon as possible during
 * driver probe to detect whether we are driving a PF or a VF device.
 * SR-IOV PF mode detection is based on PCI @dev_is_pf() function.
 * SR-IOV VF mode detection is based on MMIO register read.
 */
enum i915_iov_mode i915_sriov_probe(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (!HAS_SRIOV(i915))
		return I915_IOV_MODE_NONE;

	if (gen12_pci_capability_is_vf(pdev))
		return I915_IOV_MODE_SRIOV_VF;

#ifdef CONFIG_PCI_IOV
	if (dev_is_pf(dev) && pf_verify_readiness(i915))
		return I915_IOV_MODE_SRIOV_PF;
#endif

	return I915_IOV_MODE_NONE;
}

static int vf_check_guc_submission_support(struct drm_i915_private *i915)
{
	if (!intel_guc_submission_is_wanted(&to_gt(i915)->uc.guc)) {
		drm_err(&i915->drm, "GuC submission disabled\n");
		return -ENODEV;
	}

	return 0;
}

static void vf_tweak_device_info(struct drm_i915_private *i915)
{
	struct intel_device_info *info = mkwrite_device_info(i915);
	struct intel_runtime_info *rinfo = RUNTIME_INFO(i915);

	/* Force PCH_NOOP. We have no access to display */
	i915->pch_type = PCH_NOP;
	memset(&info->display, 0, sizeof(info->display));
	rinfo->memory_regions &= ~(REGION_STOLEN_SMEM | REGION_STOLEN_LMEM);
	rinfo->pipe_mask = 0;
}

/**
 * i915_sriov_early_tweaks - Perform early tweaks needed for SR-IOV.
 * @i915: the i915 struct
 *
 * This function should be called once and as soon as possible during
 * driver probe to perform early checks and required tweaks to
 * the driver data.
 */
int i915_sriov_early_tweaks(struct drm_i915_private *i915)
{
	int err;

	if (IS_SRIOV_VF(i915)) {
		err = vf_check_guc_submission_support(i915);
		if (unlikely(err))
			return err;
		vf_tweak_device_info(i915);
	}

	return 0;
}

int i915_sriov_pf_get_device_totalvfs(struct drm_i915_private *i915)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	return i915->sriov.pf.device_vfs;
}

int i915_sriov_pf_get_totalvfs(struct drm_i915_private *i915)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	return i915->sriov.pf.driver_vfs;
}

static void pf_set_status(struct drm_i915_private *i915, int status)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	GEM_BUG_ON(!status);
	GEM_WARN_ON(i915->sriov.pf.__status);

	i915->sriov.pf.__status = status;
}

static bool pf_checklist(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	for_each_gt(gt, i915, id) {
		if (intel_gt_has_unrecoverable_error(gt)) {
			pf_update_status(&gt->iov, -EIO, "GT wedged");
			return false;
		}
	}

	return true;
}

/**
 * i915_sriov_pf_confirm - Confirm that PF is ready to enable VFs.
 * @i915: the i915 struct
 *
 * This function shall be called by the PF when all necessary
 * initialization steps were successfully completed and PF is
 * ready to enable VFs.
 */
void i915_sriov_pf_confirm(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;
	int totalvfs = i915_sriov_pf_get_totalvfs(i915);

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	if (i915_sriov_pf_aborted(i915) || !pf_checklist(i915)) {
		dev_notice(dev, "No VFs could be associated with this PF!\n");
		pf_reduce_totalvfs(i915, 0);
		return;
	}

	dev_info(dev, "%d VFs could be associated with this PF\n", totalvfs);
	pf_set_status(i915, totalvfs);
}

/**
 * i915_sriov_pf_abort - Abort PF initialization.
 * @i915: the i915 struct
 *
 * This function should be called by the PF when some of the necessary
 * initialization steps failed and PF won't be able to manage VFs.
 */
void i915_sriov_pf_abort(struct drm_i915_private *i915, int err)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	GEM_BUG_ON(err >= 0);

	__i915_printk(i915, KERN_NOTICE, "PF aborted (%pe) %pS\n",
		      ERR_PTR(err), (void *)_RET_IP_);

	pf_set_status(i915, err);
}

/**
 * i915_sriov_pf_aborted - Check if PF initialization was aborted.
 * @i915: the i915 struct
 *
 * This function may be called by the PF to check if any previous
 * initialization step has failed.
 *
 * Return: true if already aborted
 */
bool i915_sriov_pf_aborted(struct drm_i915_private *i915)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	return i915->sriov.pf.__status < 0;
}

/**
 * i915_sriov_pf_status - Status of the PF initialization.
 * @i915: the i915 struct
 *
 * This function may be called by the PF to get its status.
 *
 * Return: number of supported VFs if PF is ready or
 *         a negative error code on failure (-EBUSY if
 *         PF initialization is still in progress).
 */
int i915_sriov_pf_status(struct drm_i915_private *i915)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	return i915->sriov.pf.__status ?: -EBUSY;
}

bool i915_sriov_pf_is_auto_provisioning_enabled(struct drm_i915_private *i915)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	return !i915->sriov.pf.disable_auto_provisioning;
}

int i915_sriov_pf_set_auto_provisioning(struct drm_i915_private *i915, bool enable)
{
	u16 num_vfs = i915_sriov_pf_get_totalvfs(i915);
	struct intel_gt *gt;
	unsigned int id;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	if (enable == i915_sriov_pf_is_auto_provisioning_enabled(i915))
		return 0;

	/* disabling is always allowed */
	if (!enable)
		goto set;

	/* enabling is only allowed if all provisioning is empty */
	for_each_gt(gt, i915, id) {
		err = intel_iov_provisioning_verify(&gt->iov, num_vfs);
		if (err == -ENODATA)
			continue;
		return -ESTALE;
	}

set:
	dev_info(i915->drm.dev, "VFs auto-provisioning was turned %s\n",
		 str_on_off(enable));

	i915->sriov.pf.disable_auto_provisioning = !enable;
	return 0;
}

/**
 * i915_sriov_print_info - Print SR-IOV information.
 * @iov: the i915 struct
 * @p: the DRM printer
 *
 * Print SR-IOV related info into provided DRM printer.
 */
void i915_sriov_print_info(struct drm_i915_private *i915, struct drm_printer *p)
{
	struct device *dev = i915->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	drm_printf(p, "supported: %s\n", str_yes_no(HAS_SRIOV(i915)));
	drm_printf(p, "enabled: %s\n", str_yes_no(IS_SRIOV(i915)));

	if (!IS_SRIOV(i915))
		return;

	drm_printf(p, "mode: %s\n", i915_iov_mode_to_string(IOV_MODE(i915)));

	if (IS_SRIOV_PF(i915)) {
		int status = i915_sriov_pf_status(i915);

		drm_printf(p, "status: %s\n", str_on_off(status > 0));
		if (status < 0)
			drm_printf(p, "error: %d (%pe)\n",
				   status, ERR_PTR(status));

		drm_printf(p, "device vfs: %u\n", i915_sriov_pf_get_device_totalvfs(i915));
		drm_printf(p, "driver vfs: %u\n", i915_sriov_pf_get_totalvfs(i915));
		drm_printf(p, "supported vfs: %u\n", pci_sriov_get_totalvfs(pdev));
		drm_printf(p, "enabled vfs: %u\n", pci_num_vf(pdev));
	}
}

static int pf_update_guc_clients(struct intel_iov *iov, unsigned int num_vfs)
{
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	err = intel_iov_provisioning_push(iov, num_vfs);
	if (unlikely(err))
		IOV_DEBUG(iov, "err=%d", err);

	return err;
}

/**
 * i915_sriov_pf_enable_vfs - Enable VFs.
 * @i915: the i915 struct
 * @num_vfs: number of VFs to enable (shall not be zero)
 *
 * This function will enable specified number of VFs. Note that VFs can be
 * enabled only after successful PF initialization.
 * This function shall be called only on PF.
 *
 * Return: number of configured VFs or a negative error code on failure.
 */
int i915_sriov_pf_enable_vfs(struct drm_i915_private *i915, int num_vfs)
{
	bool auto_provisioning = i915_sriov_pf_is_auto_provisioning_enabled(i915);
	struct device *dev = i915->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct intel_gt *gt;
	unsigned int id;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	GEM_BUG_ON(num_vfs < 0);
	drm_dbg(&i915->drm, "enabling %d VFs\n", num_vfs);

	/* verify that all initialization was successfully completed */
	err = i915_sriov_pf_status(i915);
	if (err < 0)
		goto fail;

	/* hold the reference to runtime pm as long as VFs are enabled */
	for_each_gt(gt, i915, id)
		intel_gt_pm_get_untracked(gt);

	for_each_gt(gt, i915, id) {
		err = intel_iov_provisioning_verify(&gt->iov, num_vfs);
		if (err == -ENODATA) {
			if (auto_provisioning)
				err = intel_iov_provisioning_auto(&gt->iov, num_vfs);
			else
				err = 0; /* trust late provisioning */
		}
		if (unlikely(err))
			goto fail_pm;
	}

	for_each_gt(gt, i915, id) {
		err = pf_update_guc_clients(&gt->iov, num_vfs);
		if (unlikely(err < 0))
			goto fail_pm;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err < 0)
		goto fail_guc;

	i915_sriov_sysfs_update_links(i915, true);

	dev_info(dev, "Enabled %u VFs\n", num_vfs);
	return num_vfs;

fail_guc:
	for_each_gt(gt, i915, id)
		pf_update_guc_clients(&gt->iov, 0);
fail_pm:
	for_each_gt(gt, i915, id) {
		intel_iov_provisioning_auto(&gt->iov, 0);
		intel_gt_pm_put_untracked(gt);
	}
fail:
	drm_err(&i915->drm, "Failed to enable %u VFs (%pe)\n",
		num_vfs, ERR_PTR(err));
	return err;
}

static void pf_start_vfs_flr(struct intel_iov *iov, unsigned int num_vfs)
{
	unsigned int n;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	for (n = 1; n <= num_vfs; n++)
		intel_iov_state_start_flr(iov, n);
}

#define I915_VF_FLR_TIMEOUT_MS 500

static void pf_wait_vfs_flr(struct intel_iov *iov, unsigned int num_vfs)
{
	unsigned int timeout_ms = I915_VF_FLR_TIMEOUT_MS;
	unsigned int n;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	for (n = 1; n <= num_vfs; n++) {
		if (wait_for(intel_iov_state_no_flr(iov, n), timeout_ms)) {
			IOV_ERROR(iov, "VF%u FLR didn't complete within %u ms\n",
				  n, timeout_ms);
			timeout_ms /= 2;
		}
	}
}

/**
 * i915_sriov_pf_disable_vfs - Disable VFs.
 * @i915: the i915 struct
 *
 * This function will disable all previously enabled VFs.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_pf_disable_vfs(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	u16 num_vfs = pci_num_vf(pdev);
	u16 vfs_assigned = pci_vfs_assigned(pdev);
	struct intel_gt *gt;
	unsigned int id;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	drm_dbg(&i915->drm, "disabling %u VFs\n", num_vfs);

	if (vfs_assigned) {
		dev_warn(dev, "Can't disable %u VFs, %u are still assigned\n",
			 num_vfs, vfs_assigned);
		return -EPERM;
	}

	if (!num_vfs)
		return 0;

	i915_sriov_sysfs_update_links(i915, false);

	pci_disable_sriov(pdev);

	for_each_gt(gt, i915, id)
		pf_start_vfs_flr(&gt->iov, num_vfs);
	for_each_gt(gt, i915, id)
		pf_wait_vfs_flr(&gt->iov, num_vfs);

	for_each_gt(gt, i915, id) {
		pf_update_guc_clients(&gt->iov, 0);
		intel_iov_provisioning_auto(&gt->iov, 0);
	}

	for_each_gt(gt, i915, id)
		intel_gt_pm_put_untracked(gt);

	dev_info(dev, "Disabled %u VFs\n", num_vfs);
	return 0;
}

/**
 * i915_sriov_pf_stop_vf - Stop VF.
 * @i915: the i915 struct
 * @vfid: VF identifier
 *
 * This function will stop VF on all tiles.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_pf_stop_vf(struct drm_i915_private *i915, unsigned int vfid)
{
	struct device *dev = i915->drm.dev;
	struct intel_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	for_each_gt(gt, i915, id) {
		err = intel_iov_state_stop_vf(&gt->iov, vfid);
		if (unlikely(err)) {
			dev_warn(dev, "Failed to stop VF%u on gt%u (%pe)\n",
				 vfid, id, ERR_PTR(err));
			result = result ?: err;
		}
	}

	return result;
}

/**
 * i915_sriov_pf_pause_vf - Pause VF.
 * @i915: the i915 struct
 * @vfid: VF identifier
 *
 * This function will pause VF on all tiles.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_pf_pause_vf(struct drm_i915_private *i915, unsigned int vfid)
{
	struct device *dev = i915->drm.dev;
	struct intel_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	for_each_gt(gt, i915, id) {
		err = intel_iov_state_pause_vf(&gt->iov, vfid);
		if (unlikely(err)) {
			dev_warn(dev, "Failed to pause VF%u on gt%u (%pe)\n",
				 vfid, id, ERR_PTR(err));
			result = result ?: err;
		}
	}

	return result;
}

/**
 * i915_sriov_pf_resume_vf - Resume VF.
 * @i915: the i915 struct
 * @vfid: VF identifier
 *
 * This function will resume VF on all tiles.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_pf_resume_vf(struct drm_i915_private *i915, unsigned int vfid)
{
	struct device *dev = i915->drm.dev;
	struct intel_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	for_each_gt(gt, i915, id) {
		err = intel_iov_state_resume_vf(&gt->iov, vfid);
		if (unlikely(err)) {
			dev_warn(dev, "Failed to resume VF%u on gt%u (%pe)\n",
				 vfid, id, ERR_PTR(err));
			result = result ?: err;
		}
	}

	return result;
}

/**
 * i915_sriov_pf_clear_vf - Unprovision VF.
 * @i915: the i915 struct
 * @vfid: VF identifier
 *
 * This function will uprovision VF on all tiles.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_pf_clear_vf(struct drm_i915_private *i915, unsigned int vfid)
{
	struct device *dev = i915->drm.dev;
	struct intel_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	for_each_gt(gt, i915, id) {
		err = intel_iov_provisioning_clear(&gt->iov, vfid);
		if (unlikely(err)) {
			dev_warn(dev, "Failed to unprovision VF%u on gt%u (%pe)\n",
				 vfid, id, ERR_PTR(err));
			result = result ?: err;
		}
	}

	return result;
}

/**
 * i915_sriov_suspend_late - Suspend late SR-IOV.
 * @i915: the i915 struct
 *
 * The function is called in a callback suspend_late.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_suspend_late(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	if (IS_SRIOV_PF(i915)) {
		/*
		 * When we're enabling the VFs in i915_sriov_pf_enable_vfs(), we also get
		 * a GT PM wakeref which we hold for the whole VFs life cycle.
		 * However for the time of suspend this wakeref must be put back.
		 * We'll get it back during the resume in i915_sriov_resume_early().
		 */
		if (pci_num_vf(to_pci_dev(i915->drm.dev)) != 0) {
			for_each_gt(gt, i915, id)
				intel_gt_pm_put_untracked(gt);
		}
	}

	return 0;
}

/**
 * i915_sriov_resume_early - Resume early SR-IOV.
 * @i915: the i915 struct
 *
 * The function is called in a callback resume_early.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_resume_early(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	if (IS_SRIOV_PF(i915)) {
		/*
		 * When we're enabling the VFs in i915_sriov_pf_enable_vfs(), we also get
		 * a GT PM wakeref which we hold for the whole VFs life cycle.
		 * However for the time of suspend this wakeref must be put back.
		 * If we have VFs enabled, now is the moment at which we get back this wakeref.
		 */
		if (pci_num_vf(to_pci_dev(i915->drm.dev)) != 0) {
			for_each_gt(gt, i915, id)
				intel_gt_pm_get_untracked(gt);
		}
	}

	return 0;
}
