// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_display.h"
#include "regs/xe_irq_regs.h"

#include <linux/fb.h>

#include <drm/drm_client.h>
#include <drm/drm_client_event.h>
#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <uapi/drm/xe_drm.h>

#include "soc/intel_dram.h"
#include "intel_acpi.h"
#include "intel_audio.h"
#include "intel_bw.h"
#include "intel_display.h"
#include "intel_display_core.h"
#include "intel_display_driver.h"
#include "intel_display_irq.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dmc_wl.h"
#include "intel_dp.h"
#include "intel_encoder.h"
#include "intel_fbdev.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "intel_opregion.h"
#include "skl_watermark.h"
#include "xe_module.h"

/* Xe device functions */

static bool has_display(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	return HAS_DISPLAY(display);
}

/**
 * xe_display_driver_probe_defer - Detect if we need to wait for other drivers
 *				   early on
 * @pdev: PCI device
 *
 * Note: This is called before xe or display device creation.
 *
 * Returns: true if probe needs to be deferred, false otherwise
 */
bool xe_display_driver_probe_defer(struct pci_dev *pdev)
{
	if (!xe_modparam.probe_display)
		return 0;

	return intel_display_driver_probe_defer(pdev);
}

/**
 * xe_display_driver_set_hooks - Add driver flags and hooks for display
 * @driver: DRM device driver
 *
 * Set features and function hooks in @driver that are needed for driving the
 * display IP. This sets the driver's capability of driving display, regardless
 * if the device has it enabled
 *
 * Note: This is called before xe or display device creation.
 */
void xe_display_driver_set_hooks(struct drm_driver *driver)
{
	if (!xe_modparam.probe_display)
		return;

#ifdef CONFIG_DRM_FBDEV_EMULATION
	driver->fbdev_probe = intel_fbdev_driver_fbdev_probe;
#endif

	driver->driver_features |= DRIVER_MODESET | DRIVER_ATOMIC;
}

static void unset_display_features(struct xe_device *xe)
{
	xe->drm.driver_features &= ~(DRIVER_MODESET | DRIVER_ATOMIC);
}

static void xe_display_fini_early(void *arg)
{
	struct xe_device *xe = arg;
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_display_driver_remove_nogem(display);
	intel_display_driver_remove_noirq(display);
	intel_opregion_cleanup(display);
	intel_power_domains_cleanup(display);
}

int xe_display_init_early(struct xe_device *xe)
{
	struct intel_display *display = xe->display;
	int err;

	if (!xe->info.probe_display)
		return 0;

	/* Fake uncore lock */
	spin_lock_init(&xe->uncore.lock);

	intel_display_driver_early_probe(display);

	/* Early display init.. */
	intel_opregion_setup(display);

	/*
	 * Fill the dram structure to get the system dram info. This will be
	 * used for memory latency calculation.
	 */
	err = intel_dram_detect(xe);
	if (err)
		goto err_opregion;

	intel_bw_init_hw(display);

	intel_display_device_info_runtime_init(display);

	err = intel_display_driver_probe_noirq(display);
	if (err)
		goto err_opregion;

	err = intel_display_driver_probe_nogem(display);
	if (err)
		goto err_noirq;

	return devm_add_action_or_reset(xe->drm.dev, xe_display_fini_early, xe);
err_noirq:
	intel_display_driver_remove_noirq(display);
	intel_power_domains_cleanup(display);
err_opregion:
	intel_opregion_cleanup(display);
	return err;
}

static void xe_display_fini(void *arg)
{
	struct xe_device *xe = arg;
	struct intel_display *display = xe->display;

	intel_hpd_poll_fini(display);
	intel_hdcp_component_fini(display);
	intel_audio_deinit(display);
	intel_display_driver_remove(display);
}

int xe_display_init(struct xe_device *xe)
{
	struct intel_display *display = xe->display;
	int err;

	if (!xe->info.probe_display)
		return 0;

	err = intel_display_driver_probe(display);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, xe_display_fini, xe);
}

void xe_display_register(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_display_driver_register(display);
	intel_power_domains_enable(display);
}

void xe_display_unregister(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_power_domains_disable(display);
	intel_display_driver_unregister(display);
}

/* IRQ-related functions */

void xe_display_irq_handler(struct xe_device *xe, u32 master_ctl)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	if (master_ctl & DISPLAY_IRQ)
		gen11_display_irq_handler(display);
}

void xe_display_irq_enable(struct xe_device *xe, u32 gu_misc_iir)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	if (gu_misc_iir & GU_MISC_GSE)
		intel_opregion_asle_intr(display);
}

void xe_display_irq_reset(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	gen11_display_irq_reset(display);
}

void xe_display_irq_postinstall(struct xe_device *xe, struct xe_gt *gt)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	if (gt->info.id == XE_GT0)
		gen11_de_irq_postinstall(display);
}

static bool suspend_to_idle(void)
{
#if IS_ENABLED(CONFIG_ACPI_SLEEP)
	if (acpi_target_system_state() < ACPI_STATE_S3)
		return true;
#endif
	return false;
}

static void xe_display_flush_cleanup_work(struct xe_device *xe)
{
	struct intel_crtc *crtc;

	for_each_intel_crtc(&xe->drm, crtc) {
		struct drm_crtc_commit *commit;

		spin_lock(&crtc->base.commit_lock);
		commit = list_first_entry_or_null(&crtc->base.commit_list,
						  struct drm_crtc_commit, commit_entry);
		if (commit)
			drm_crtc_commit_get(commit);
		spin_unlock(&crtc->base.commit_lock);

		if (commit) {
			wait_for_completion(&commit->cleanup_done);
			drm_crtc_commit_put(commit);
		}
	}
}

static void xe_display_enable_d3cold(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	/*
	 * We do a lot of poking in a lot of registers, make sure they work
	 * properly.
	 */
	intel_power_domains_disable(display);

	xe_display_flush_cleanup_work(xe);

	intel_opregion_suspend(display, PCI_D3cold);

	intel_dmc_suspend(display);

	if (has_display(xe))
		intel_hpd_poll_enable(display);
}

static void xe_display_disable_d3cold(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_dmc_resume(display);

	if (has_display(xe))
		drm_mode_config_reset(&xe->drm);

	intel_display_driver_init_hw(display);

	intel_hpd_init(display);

	if (has_display(xe))
		intel_hpd_poll_disable(display);

	intel_opregion_resume(display);

	intel_power_domains_enable(display);
}

void xe_display_pm_suspend(struct xe_device *xe)
{
	struct intel_display *display = xe->display;
	bool s2idle = suspend_to_idle();

	if (!xe->info.probe_display)
		return;

	/*
	 * We do a lot of poking in a lot of registers, make sure they work
	 * properly.
	 */
	intel_power_domains_disable(display);
	drm_client_dev_suspend(&xe->drm, false);

	if (has_display(xe)) {
		drm_kms_helper_poll_disable(&xe->drm);
		intel_display_driver_disable_user_access(display);
		intel_display_driver_suspend(display);
	}

	xe_display_flush_cleanup_work(xe);

	intel_hpd_cancel_work(display);

	if (has_display(xe)) {
		intel_display_driver_suspend_access(display);
		intel_encoder_suspend_all(display);
	}

	intel_opregion_suspend(display, s2idle ? PCI_D1 : PCI_D3cold);

	intel_dmc_suspend(display);
}

void xe_display_pm_shutdown(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_power_domains_disable(display);
	drm_client_dev_suspend(&xe->drm, false);

	if (has_display(xe)) {
		drm_kms_helper_poll_disable(&xe->drm);
		intel_display_driver_disable_user_access(display);
		intel_display_driver_suspend(display);
	}

	xe_display_flush_cleanup_work(xe);
	intel_dp_mst_suspend(display);
	intel_hpd_cancel_work(display);

	if (has_display(xe))
		intel_display_driver_suspend_access(display);

	intel_encoder_suspend_all(display);
	intel_encoder_shutdown_all(display);

	intel_opregion_suspend(display, PCI_D3cold);

	intel_dmc_suspend(display);
}

void xe_display_pm_runtime_suspend(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	if (xe->d3cold.allowed) {
		xe_display_enable_d3cold(xe);
		return;
	}

	intel_hpd_poll_enable(display);
}

void xe_display_pm_suspend_late(struct xe_device *xe)
{
	struct intel_display *display = xe->display;
	bool s2idle = suspend_to_idle();

	if (!xe->info.probe_display)
		return;

	intel_display_power_suspend_late(display, s2idle);
}

void xe_display_pm_runtime_suspend_late(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	if (xe->d3cold.allowed)
		xe_display_pm_suspend_late(xe);

	/*
	 * If xe_display_pm_suspend_late() is not called, it is likely
	 * that we will be on dynamic DC states with DMC wakelock enabled. We
	 * need to flush the release work in that case.
	 */
	intel_dmc_wl_flush_release_work(display);
}

void xe_display_pm_shutdown_late(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	/*
	 * The only requirement is to reboot with display DC states disabled,
	 * for now leaving all display power wells in the INIT power domain
	 * enabled.
	 */
	intel_power_domains_driver_remove(display);
}

void xe_display_pm_resume_early(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_display_power_resume_early(display);
}

void xe_display_pm_resume(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	intel_dmc_resume(display);

	if (has_display(xe))
		drm_mode_config_reset(&xe->drm);

	intel_display_driver_init_hw(display);

	if (has_display(xe))
		intel_display_driver_resume_access(display);

	intel_hpd_init(display);

	if (has_display(xe)) {
		intel_display_driver_resume(display);
		drm_kms_helper_poll_enable(&xe->drm);
		intel_display_driver_enable_user_access(display);
	}

	if (has_display(xe))
		intel_hpd_poll_disable(display);

	intel_opregion_resume(display);

	drm_client_dev_resume(&xe->drm, false);

	intel_power_domains_enable(display);
}

void xe_display_pm_runtime_resume(struct xe_device *xe)
{
	struct intel_display *display = xe->display;

	if (!xe->info.probe_display)
		return;

	if (xe->d3cold.allowed) {
		xe_display_disable_d3cold(xe);
		return;
	}

	intel_hpd_init(display);
	intel_hpd_poll_disable(display);
	skl_watermark_ipc_update(display);
}


static void display_device_remove(struct drm_device *dev, void *arg)
{
	struct intel_display *display = arg;

	intel_display_device_remove(display);
}

/**
 * xe_display_probe - probe display and create display struct
 * @xe: XE device instance
 *
 * Initialize all fields used by the display part.
 *
 * TODO: once everything can be inside a single struct, make the struct opaque
 * to the rest of xe and return it to be xe->display.
 *
 * Returns: 0 on success
 */
int xe_display_probe(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct intel_display *display;
	int err;

	if (!xe->info.probe_display)
		goto no_display;

	display = intel_display_device_probe(pdev);
	if (IS_ERR(display))
		return PTR_ERR(display);

	err = drmm_add_action_or_reset(&xe->drm, display_device_remove, display);
	if (err)
		return err;

	xe->display = display;

	if (has_display(xe))
		return 0;

no_display:
	xe->info.probe_display = false;
	unset_display_features(xe);
	return 0;
}
