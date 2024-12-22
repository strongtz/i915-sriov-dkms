// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/i915_sriov.h>

#include "i915_sriov.h"
#include "i915_sriov_sysfs.h"
#include "i915_drv.h"
#include "i915_pci.h"
#include "i915_utils.h"
#include "i915_reg.h"
#include "intel_pci_config.h"

#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/iov/intel_iov_provisioning.h"
#include "gt/iov/intel_iov_service.h"
#include "gt/iov/intel_iov_reg.h"
#include "gt/iov/intel_iov_state.h"
#include "gt/iov/intel_iov_utils.h"

#include "pxp/intel_pxp.h"

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
/* XXX: can't use drm_WARN() as we are still using preliminary IP versions at few locations */
void assert_graphics_ip_ver_ready(const struct drm_i915_private *i915)
{
	if (RUNTIME_INFO(i915)->graphics.ip.preliminary)
		drm_info(&i915->drm, "preliminary %s version %u.%02u used at %pS", "graphics",
			 RUNTIME_INFO(i915)->graphics.ip.ver, RUNTIME_INFO(i915)->graphics.ip.rel,
			 (void *)_RET_IP_);
}
void assert_media_ip_ver_ready(const struct drm_i915_private *i915)
{
	if (RUNTIME_INFO(i915)->media.ip.preliminary)
		drm_info(&i915->drm, "preliminary %s version %u.%02u used at %pS", "media",
			 RUNTIME_INFO(i915)->media.ip.ver, RUNTIME_INFO(i915)->media.ip.rel,
			 (void *)_RET_IP_);
}
#endif

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

	if ((RUNTIME_INFO(i915)->memory_regions & BIT(INTEL_REGION_LMEM_0)) &&
	    !i915_pci_resource_valid(pdev, GEN12_VF_LMEM_BAR))
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

extern const struct intel_display_device_info no_display;

static void vf_tweak_device_info(struct drm_i915_private *i915)
{
	/* FIXME: info shouldn't be written to outside of intel_device_info.c */
	struct intel_display_runtime_info *drinfo = DISPLAY_RUNTIME_INFO(i915);

	/* Force PCH_NOOP. We have no access to display */
	i915->pch_type = PCH_NOP;
	i915->display.info.__device_info = &no_display;

	/*
	 * Overwrite current display runtime info based on just updated device
	 * info for VF.
	 */
	memcpy(drinfo, &i915->display.info.__device_info->__runtime_defaults, sizeof(*drinfo));
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
	struct intel_gt *gt;
	unsigned int id;
	intel_wakeref_t wakeref;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	if (i915_sriov_pf_aborted(i915) || !pf_checklist(i915)) {
		dev_notice(dev, "No VFs could be associated with this PF!\n");
		pf_reduce_totalvfs(i915, 0);
		return;
	}

	dev_info(dev, "%d VFs could be associated with this PF\n", totalvfs);
	pf_set_status(i915, totalvfs);

	/*
	 * FIXME: Temporary solution to force VGT mode in GuC throughout
	 * the life cycle of the PF.
	 */
	for_each_gt(gt, i915, id)
		with_intel_runtime_pm(gt->uncore->rpm, wakeref)
			intel_iov_provisioning_force_vgt_mode(&gt->iov);

}

/**
 * i915_sriov_pf_abort - Abort PF initialization.
 * @i915: the i915 struct
 * @err: error code that caused abort
 *
 * This function should be called by the PF when some of the necessary
 * initialization steps failed and PF won't be able to manage VFs.
 */
void i915_sriov_pf_abort(struct drm_i915_private *i915, int err)
{
	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	GEM_BUG_ON(err >= 0);

	drm_info(&i915->drm, "PF aborted (%pe) %pS\n",
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
 * @i915: the i915 struct
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

static int pf_enable_gsc_engine(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	for_each_gt(gt, i915, id) {
		err = intel_guc_enable_gsc_engine(&gt->uc.guc);
		if (err < 0)
			return err;
	}

	err = intel_pxp_init(i915);
	/*
	 * XXX: Ignore -ENODEV error.
	 * It this case, there is no need to reinitialize PXP
	 */
	return (err < 0 && err != -ENODEV) ? err : 0;
}

static int pf_disable_gsc_engine(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	for_each_gt(gt, i915, id)
		intel_gsc_uc_flush_work(&gt->uc.gsc);

	intel_pxp_fini(i915);

	for_each_gt(gt, i915, id) {
		int err = intel_guc_disable_gsc_engine(&gt->uc.guc);

		if (err < 0)
			return err;
	}

	return 0;
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

	/* Wa_14019103365 */
	if (IS_METEORLAKE(i915)) {
		err = pf_disable_gsc_engine(i915);
		if (err)
			drm_warn(&i915->drm, "Failed to disable GSC engine (%pe)\n", ERR_PTR(err));
	}

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

		/*
		 * Update cached values of runtime registers shared with the VFs in case
		 * HuC status register has been updated by the GSC after our initial probe.
		 */
		intel_iov_service_update(&gt->iov);
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

#define I915_VF_FLR_TIMEOUT_MS 1000

static unsigned int pf_wait_vfs_flr(struct intel_iov *iov, unsigned int num_vfs,
				    unsigned int timeout_ms)
{
	unsigned int timed_out = 0;
	unsigned int n;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	for (n = 1; n <= num_vfs; n++) {
		if (wait_for(intel_iov_state_no_flr(iov, n), timeout_ms)) {
			IOV_ERROR(iov, "VF%u FLR didn't complete within %u ms\n",
				  n, timeout_ms);
			timeout_ms /= 2;
			timed_out++;
		}
	}
	return timed_out;
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
	unsigned int in_flr = 0;
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
		pf_wait_vfs_flr(&gt->iov, num_vfs, I915_VF_FLR_TIMEOUT_MS);

	for_each_gt(gt, i915, id) {
		/* unprovisioning won't work if FLR didn't finish */
		in_flr = pf_wait_vfs_flr(&gt->iov, num_vfs, 0);
		if (in_flr) {
			gt_warn(gt, "Can't unprovision %u VFs, %u FLRs are still in progress\n",
				 num_vfs, in_flr);
			continue;
		}
		pf_update_guc_clients(&gt->iov, 0);
		intel_iov_provisioning_auto(&gt->iov, 0);
	}

	/* Wa_14019103365 */
	if (IS_METEORLAKE(i915)) {
		int err = pf_enable_gsc_engine(i915);

		if (err)
			dev_warn(dev, "Failed to re-enable GSC engine (%pe)\n", ERR_PTR(err));
	}

	for_each_gt(gt, i915, id)
		intel_gt_pm_put_untracked(gt);

	dev_info(dev, "Disabled %u VFs\n", num_vfs);
	return 0;
}

static bool needs_save_restore(struct drm_i915_private *i915, unsigned int vfid)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct pci_dev *vfpdev = i915_pci_pf_get_vf_dev(pdev, vfid);
	bool ret;

	if (!vfpdev)
		return false;

	/*
	 * If VF has the same driver as PF loaded (from host perspective), we don't need
	 * to save/restore its state, because the VF driver will receive the same PM
	 * handling as all the host drivers. There is also no need to save/restore state
	 * when no driver is loaded on VF.
	 */
	ret = (vfpdev->driver && strcmp(vfpdev->driver->name, pdev->driver->name) != 0);

	pci_dev_put(vfpdev);
	return ret;
}

static void pf_restore_vfs_pci_state(struct drm_i915_private *i915, unsigned int num_vfs)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int vfid;

	GEM_BUG_ON(num_vfs > pci_num_vf(pdev));

	for (vfid = 1; vfid <= num_vfs; vfid++) {
		struct pci_dev *vfpdev = i915_pci_pf_get_vf_dev(pdev, vfid);

		if (!vfpdev)
			continue;
		if (!needs_save_restore(i915, vfid))
			continue;

		/*
		 * XXX: Waiting for other drivers to do their job.
		 * We can ignore the potential error in this function -
		 * in case of an error, we still want to try to reinitialize
		 * the MSI and set the PCI master.
		 */
		device_pm_wait_for_dev(&pdev->dev, &vfpdev->dev);

		pci_restore_msi_state(vfpdev);
		pci_set_master(vfpdev);

		pci_dev_put(vfpdev);
	}
}

#define I915_VF_REPROVISION_TIMEOUT_MS 1000

static int pf_gt_save_vf_running(struct intel_gt *gt, unsigned int vfid)
{
	struct pci_dev *pdev = to_pci_dev(gt->i915->drm.dev);
	struct intel_iov *iov = &gt->iov;

	GEM_BUG_ON(!vfid);
	GEM_BUG_ON(vfid > pci_num_vf(pdev));

	return intel_iov_state_pause_vf_sync(iov, vfid, true);
}

static void pf_save_vfs_running(struct drm_i915_private *i915, unsigned int num_vfs)
{
	unsigned int saved = 0;
	struct intel_gt *gt;
	unsigned int gt_id;
	unsigned int vfid;

	for (vfid = 1; vfid <= num_vfs; vfid++) {
		if (!needs_save_restore(i915, vfid)) {
			drm_dbg(&i915->drm, "Save of VF%u running state has been skipped\n", vfid);
			continue;
		}

		for_each_gt(gt, i915, gt_id) {
			int err = pf_gt_save_vf_running(gt, vfid);

			if (err < 0)
				goto skip_vf;
		}
		saved++;
		continue;
skip_vf:
		break;
	}

	drm_dbg(&i915->drm, "%u of %u VFs running state successfully saved", saved, num_vfs);
}

static int pf_gt_save_vf_guc_state(struct intel_gt *gt, unsigned int vfid)
{
	struct pci_dev *pdev = to_pci_dev(gt->i915->drm.dev);
	struct intel_iov *iov = &gt->iov;
	struct intel_iov_data *data = &iov->pf.state.data[vfid];
	int ret, size;

	GEM_BUG_ON(!vfid);
	GEM_BUG_ON(vfid > pci_num_vf(pdev));

	ret = intel_iov_state_save_vf_size(iov, vfid);
	if (unlikely(ret < 0)) {
		IOV_ERROR(iov, "Failed to get size of VF%u GuC state: (%pe)", vfid, ERR_PTR(ret));
		return ret;
	}
	size = ret;

	if (data->guc_state.blob && size <= data->guc_state.size) {
		memset(data->guc_state.blob, 0, data->guc_state.size);
	} else {
		void *prev_state;

		prev_state = fetch_and_zero(&data->guc_state.blob);
		kfree(prev_state);
		data->guc_state.blob = kzalloc(size, GFP_KERNEL);
		data->guc_state.size = size;
	}

	if (!data->guc_state.blob) {
		ret = -ENOMEM;
		goto error;
	}

	ret = intel_iov_state_save_vf(iov, vfid, data->guc_state.blob, data->guc_state.size);
error:
	if (unlikely(ret < 0)) {
		IOV_ERROR(iov, "Failed to save VF%u GuC state: (%pe)", vfid, ERR_PTR(ret));
		return ret;
	}

	return ret;
}

static void pf_save_vfs_guc_state(struct drm_i915_private *i915, unsigned int num_vfs)
{
	unsigned int saved = 0;
	struct intel_gt *gt;
	unsigned int gt_id;
	unsigned int vfid;

	for (vfid = 1; vfid <= num_vfs; vfid++) {
		if (!needs_save_restore(i915, vfid)) {
			drm_dbg(&i915->drm, "Save of VF%u GuC state has been skipped\n", vfid);
			continue;
		}

		for_each_gt(gt, i915, gt_id) {
			int err = pf_gt_save_vf_guc_state(gt, vfid);

			if (err < 0)
				goto skip_vf;
		}
		saved++;
		continue;
skip_vf:
		break;
	}

	drm_dbg(&i915->drm, "%u of %u VFs GuC state successfully saved", saved, num_vfs);
}

static bool guc_supports_save_restore_v2(struct intel_guc *guc)
{
	return MAKE_GUC_VER_STRUCT(guc->fw.file_selected.ver)
	       >= MAKE_GUC_VER(70, 25, 0);
}

static int pf_gt_restore_vf_guc_state(struct intel_gt *gt, unsigned int vfid)
{
	struct pci_dev *pdev = to_pci_dev(gt->i915->drm.dev);
	struct intel_iov *iov = &gt->iov;
	struct intel_iov_data *data = &iov->pf.state.data[vfid];
	unsigned long timeout_ms = I915_VF_REPROVISION_TIMEOUT_MS;
	int err;

	GEM_BUG_ON(!vfid);
	GEM_BUG_ON(vfid > pci_num_vf(pdev));

	if (!data->guc_state.blob)
		return -EINVAL;

	if (wait_for(iov->pf.provisioning.num_pushed >= vfid, timeout_ms)) {
		IOV_ERROR(iov,
			  "Failed to restore VF%u GuC state. Provisioning didn't complete within %lu ms\n",
			  vfid, timeout_ms);
		return -ETIMEDOUT;
	}

	/*
	 * For save/restore v2, GuC requires the VF to be in paused state
	 * before restore. However, after suspend, VF is in ready state.
	 * So in order to restore the GuC state, we must first pause the VF
	 */
	if (guc_supports_save_restore_v2(&gt->uc.guc)) {
		err = intel_iov_state_pause_vf_sync(iov, vfid, true);
		if (err < 0)
			return err;
	}

	err = intel_iov_state_restore_vf(iov, vfid, data->guc_state.blob, data->guc_state.size);
	if (err < 0) {
		IOV_ERROR(iov, "Failed to restore VF%u GuC state: (%pe)", vfid, ERR_PTR(err));
		return err;
	}

	kfree(data->guc_state.blob);
	data->guc_state.blob = NULL;

	return 0;
}

static void pf_restore_vfs_guc_state(struct drm_i915_private *i915, unsigned int num_vfs)
{
	unsigned int restored = 0;
	struct intel_gt *gt;
	unsigned int gt_id;
	unsigned int vfid;

	for (vfid = 1; vfid <= num_vfs; vfid++) {
		if (!needs_save_restore(i915, vfid)) {
			drm_dbg(&i915->drm, "Restoration of VF%u GuC state has been skipped\n",
				vfid);
			continue;
		}

		for_each_gt(gt, i915, gt_id) {
			int err = pf_gt_restore_vf_guc_state(gt, vfid);

			if (err < 0)
				goto skip_vf;
		}
		restored++;
		continue;
skip_vf:
		break;
	}

	drm_dbg(&i915->drm, "%u of %u VFs GuC state restored successfully", restored, num_vfs);
}

static i915_reg_t vf_master_irq(struct drm_i915_private *i915, unsigned int vfid)
{
	return (GRAPHICS_VER_FULL(i915) < IP_VER(12, 50)) ?
		GEN12_VF_GFX_MSTR_IRQ(vfid) :
		XEHPSDV_VF_GFX_MSTR_IRQ(vfid);
}

static void pf_restore_vfs_irqs(struct drm_i915_private *i915, unsigned int num_vfs)
{
	struct intel_gt *gt;
	unsigned int gt_id;

	for_each_gt(gt, i915, gt_id) {
		unsigned int vfid;

		for (vfid = 1; vfid <= num_vfs; vfid++)
			raw_reg_write(gt->uncore->regs, vf_master_irq(i915, vfid),
				      GEN11_MASTER_IRQ);
	}
}

static int pf_gt_restore_vf_running(struct intel_gt *gt, unsigned int vfid)
{
	struct intel_iov *iov = &gt->iov;

	if (!test_and_clear_bit(IOV_VF_PAUSE_BY_SUSPEND, &iov->pf.state.data[vfid].state))
		return 0;

	return intel_iov_state_resume_vf(iov, vfid);
}

static void pf_restore_vfs_running(struct drm_i915_private *i915, unsigned int num_vfs)
{
	unsigned int running = 0;
	struct intel_gt *gt;
	unsigned int gt_id;
	unsigned int vfid;

	for (vfid = 1; vfid <= num_vfs; vfid++) {
		for_each_gt(gt, i915, gt_id) {
			int err = pf_gt_restore_vf_running(gt, vfid);

			if (err < 0)
				goto skip_vf;
		}
		running++;
skip_vf:
		continue;
	}

	drm_dbg(&i915->drm, "%u of %u VFs restored to proper running state", running, num_vfs);
}

static void pf_suspend_active_vfs(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int num_vfs = pci_num_vf(pdev);

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	if (num_vfs == 0)
		return;

	pf_save_vfs_running(i915, num_vfs);
	pf_save_vfs_guc_state(i915, num_vfs);
}

static void pf_resume_active_vfs(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int num_vfs = pci_num_vf(pdev);

	GEM_BUG_ON(!IS_SRIOV_PF(i915));

	if (num_vfs == 0)
		return;

	pf_restore_vfs_pci_state(i915, num_vfs);
	pf_restore_vfs_guc_state(i915, num_vfs);
	pf_restore_vfs_irqs(i915, num_vfs);
	pf_restore_vfs_running(i915, num_vfs);
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
 * i915_sriov_pause_vf - Pause VF.
 * @pdev: the i915 struct
 * @vfid: VF identifier
 *
 * This function will pause VF on all tiles.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_pause_vf(struct pci_dev *pdev, unsigned int vfid)
{
	struct drm_i915_private *i915 = pci_get_drvdata(pdev);

	if (!IS_SRIOV_PF(i915))
		return -ENODEV;

	return i915_sriov_pf_pause_vf(i915, vfid);
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_pause_vf, I915_SRIOV_NS);

/**
 * i915_sriov_resume_vf - Resume VF.
 * @pdev: the i915 struct
 * @vfid: VF identifier
 *
 * This function will resume VF on all tiles.
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_resume_vf(struct pci_dev *pdev, unsigned int vfid)
{
	struct drm_i915_private *i915 = pci_get_drvdata(pdev);

	if (!IS_SRIOV_PF(i915))
		return -ENODEV;

	return i915_sriov_pf_resume_vf(i915, vfid);
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_resume_vf, I915_SRIOV_NS);

/**
 * i915_sriov_wait_vf_flr_done - Wait for VF FLR completion.
 * @pdev: PF pci device
 * @vfid: VF identifier
 *
 * This function will wait until VF FLR is processed by PF on all tiles (or
 * until timeout occurs).
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_wait_vf_flr_done(struct pci_dev *pdev, unsigned int vfid)
{
	struct drm_i915_private *i915 = pci_get_drvdata(pdev);
	struct intel_gt *gt;
	unsigned int id;
	int ret;

	if (!IS_SRIOV_PF(i915))
		return -ENODEV;

	for_each_gt(gt, i915, id) {
		ret = wait_for(intel_iov_state_no_flr(&gt->iov, vfid), I915_VF_FLR_TIMEOUT_MS);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_wait_vf_flr_done, I915_SRIOV_NS);

static struct intel_gt *
sriov_to_gt(struct pci_dev *pdev, unsigned int tile)
{
	struct drm_i915_private *i915 = pci_get_drvdata(pdev);
	struct intel_gt *gt;

	if (!i915 || !IS_SRIOV_PF(i915))
		return NULL;

	if (!HAS_EXTRA_GT_LIST(i915) && tile > 0)
		return NULL;

	gt = NULL;
	if (tile < ARRAY_SIZE(i915->gt))
		gt = i915->gt[tile];

	return gt;
}

/**
 * i915_sriov_ggtt_size - Get size needed to store VF GGTT.
 * @pdev: PF pci device
 * @vfid: VF identifier
 * @tile: tile identifier
 *
 * This function shall be called only on PF.
 *
 * Return: Size in bytes.
 */
ssize_t
i915_sriov_ggtt_size(struct pci_dev *pdev, unsigned int vfid, unsigned int tile)
{
	struct intel_gt *gt;
	ssize_t size;

	gt = sriov_to_gt(pdev, tile);
	if (!gt)
		return 0;

	if (gt->type == GT_MEDIA)
		return 0;

	size = intel_iov_state_save_ggtt(&gt->iov, vfid, NULL, 0);
	WARN_ON(size < 0);

	return size;
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_ggtt_size, I915_SRIOV_NS);

/**
 * i915_sriov_ggtt_save - Save VF GGTT.
 * @pdev: PF pci device
 * @vfid: VF identifier
 * @tile: tile identifier
 * @buf: buffer to save VF GGTT
 * @size: size of buffer to save VF GGTT
 *
 * This function shall be called only on PF.
 *
 * Return: Size of data written on success or a negative error code on failure.
 */
ssize_t i915_sriov_ggtt_save(struct pci_dev *pdev, unsigned int vfid, unsigned int tile,
			     void *buf, size_t size)
{
	struct intel_gt *gt;

	gt = sriov_to_gt(pdev, tile);
	if (!gt)
		return -ENODEV;

	if (gt->type == GT_MEDIA)
		return -ENODEV;

	WARN_ON(buf == NULL && size == 0);

	return intel_iov_state_save_ggtt(&gt->iov, vfid, buf, size);
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_ggtt_save, I915_SRIOV_NS);

/**
 * i915_sriov_ggtt_load - Load VF GGTT.
 * @pdev: PF pci device
 * @vfid: VF identifier
 * @tile: tile identifier
 * @buf: buffer with VF GGTT
 * @size: size of buffer with VF GGTT
 *
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int
i915_sriov_ggtt_load(struct pci_dev *pdev, unsigned int vfid, unsigned int tile,
		     const void *buf, size_t size)
{
	struct intel_gt *gt;

	gt = sriov_to_gt(pdev, tile);
	if (!gt)
		return -ENODEV;

	if (gt->type == GT_MEDIA)
		return -ENODEV;

	return intel_iov_state_restore_ggtt(&gt->iov, vfid, buf, size);
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_ggtt_load, I915_SRIOV_NS);

/**
 * i915_sriov_fw_state_size - Get size needed to store GuC FW state.
 * @pdev: PF pci device
 * @vfid: VF identifier
 * @tile: tile identifier
 *
 * This function shall be called only on PF.
 *
 * Return: size in bytes on success or a negative error code on failure.
 */
ssize_t
i915_sriov_fw_state_size(struct pci_dev *pdev, unsigned int vfid, unsigned int tile)
{
	struct intel_gt *gt;
	int ret;

	gt = sriov_to_gt(pdev, tile);
	if (!gt)
		return -ENODEV;

	ret = intel_iov_state_save_vf_size(&gt->iov, vfid);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_fw_state_size, I915_SRIOV_NS);

/**
 * i915_sriov_fw_state_save - Save GuC FW state.
 * @pdev: PF pci device
 * @vfid: VF identifier
 * @tile: tile identifier
 * @buf: buffer to save GuC FW state
 * @size: size of buffer to save GuC FW state, in bytes
 *
 * This function shall be called only on PF.
 *
 * Return: Size of data written (in bytes) on success or a negative error code on failure.
 */
ssize_t
i915_sriov_fw_state_save(struct pci_dev *pdev, unsigned int vfid, unsigned int tile,
			 void *buf, size_t size)
{
	struct intel_gt *gt;
	int ret;

	gt = sriov_to_gt(pdev, tile);
	if (!gt)
		return -ENODEV;

	ret = intel_iov_state_save_vf(&gt->iov, vfid, buf, size);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_fw_state_save, I915_SRIOV_NS);

/**
 * i915_sriov_fw_state_load - Load GuC FW state.
 * @pdev: PF pci device
 * @vfid: VF identifier
 * @tile: tile identifier
 * @buf: buffer with GuC FW state to load
 * @size: size of buffer with GuC FW state
 *
 * This function shall be called only on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int
i915_sriov_fw_state_load(struct pci_dev *pdev, unsigned int vfid, unsigned int tile,
			 const void *buf, size_t size)
{
	struct intel_gt *gt;

	gt = sriov_to_gt(pdev, tile);
	if (!gt)
		return -ENODEV;

	return intel_iov_state_store_guc_migration_state(&gt->iov, vfid, buf, size);
}
EXPORT_SYMBOL_NS_GPL(i915_sriov_fw_state_load, I915_SRIOV_NS);

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
 * i915_sriov_suspend_prepare - Prepare SR-IOV to suspend.
 * @i915: the i915 struct
 *
 * The function is called in a callback prepare.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_suspend_prepare(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	if (IS_SRIOV_PF(i915)) {
		/*
		 * When we're enabling the VFs in i915_sriov_pf_enable_vfs(),
		 * we also get a GT PM wakeref which we hold for the whole VFs
		 * life cycle.
		 * However for the time of suspend this wakeref must be put back.
		 * We'll get it back during the resume in i915_sriov_resume().
		 */
		if (pci_num_vf(to_pci_dev(i915->drm.dev)) != 0) {
			for_each_gt(gt, i915, id)
				intel_gt_pm_put_untracked(gt);
		}

		pf_suspend_active_vfs(i915);
	}

	return 0;
}

/**
 * i915_sriov_resume - Resume SR-IOV.
 * @i915: the i915 struct
 *
 * The function is called in a callback resume.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_resume(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	if (IS_SRIOV_PF(i915)) {
		pf_resume_active_vfs(i915);
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
