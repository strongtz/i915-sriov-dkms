# Kernel version
KERNELRELEASE       ?= $(shell uname -r)
KERNELVERSION       := $(shell var=$(KERNELRELEASE); echo $${var%%-*})

# Kernel extraversion (separated by dashes)
EXTRAVERSION        := $(shell var=$(KERNELRELEASE); echo $${var#*-})
EXTRAVERSION_MAJOR  := $(shell var=$(EXTRAVERSION); var=$$(echo $${var%-*} | awk -F. '{x=$$1+0; print x}'); echo $${var:-0})
EXTRAVERSION_MINOR  := $(shell var=$(EXTRAVERSION); var=$$(echo $${var%-*} | awk -F. '{x=$$2+0; print x}'); echo $${var:-0})
EXTRAVERSION_NAME   := $(shell var=$(EXTRAVERSION); echo $${var#*-})
EXTRAVERSION_DEFINE := $(shell var=$(EXTRAVERSION_NAME); var=$$(echo $$var  | sed 's/-/_/'| awk '{print toupper($$0)}'); echo EXTRAVERSION_$${var:-EMPTY})

# LSB release
LSBRELEASE          := $(shell lsb_release -rs 2> /dev/null || cat /etc/*-release | grep '^VERSION_ID=' | head -n1 | cut -d '=' -f2 | xargs)
LSBRELEASE_MAJOR    := $(shell var=$$(echo $(LSBRELEASE) | awk -F. '{x=$$1+0; print x}'); echo $${var:-0})
LSBRELEASE_MINOR    := $(shell var=$$(echo $(LSBRELEASE) | awk -F. '{x=$$2+0; print x}'); echo $${var:-0})
LSBRELEASE_NAME     := $(shell lsb_release -is 2> /dev/null || cat /etc/*-release | grep '^ID=' | head -n1 | cut -d '=' -f2 | xargs)
LSBRELEASE_DEFINE   := $(shell var=$(LSBRELEASE_NAME); var=$$(echo $$var | sed 's/-/_/' | awk '{print toupper($$0)}'); echo RELEASE_$${var:-EMPTY})

# Option to override latest GuC firmware (default is API 1.9.0 / v70.20.0)
# https://patchwork.kernel.org/project/intel-gfx/patch/20240216211432.519411-1-John.C.Harrison@Intel.com/
# https://gitlab.com/kernel-firmware/linux-firmware/-/merge_requests/156
GUCFIRMWARE_MAJOR ?= $(shell echo $${GUCFIRMWARE_MAJOR:-1})
GUCFIRMWARE_MINOR ?= $(shell echo $${GUCFIRMWARE_MINOR:-9})

version:
$(info KERNELRELEASE=$(KERNELRELEASE))
$(info KERNELVERSION=$(KERNELVERSION))
$(info EXTRAVERSION_MAJOR=$(EXTRAVERSION_MAJOR))
$(info EXTRAVERSION_MINOR=$(EXTRAVERSION_MINOR))
$(info EXTRAVERSION_NAME=$(EXTRAVERSION_NAME))
$(info EXTRAVERSION_DEFINE=$(EXTRAVERSION_DEFINE))
$(info LSBRELEASE=$(LSBRELEASE))
$(info LSBRELEASE_MAJOR=$(LSBRELEASE_MAJOR))
$(info LSBRELEASE_MINOR=$(LSBRELEASE_MINOR))
$(info LSBRELEASE_NAME=$(LSBRELEASE_NAME))
$(info LSBRELEASE_DEFINE=$(LSBRELEASE_DEFINE))
$(info GUCFIRMWARE_MAJOR=$(GUCFIRMWARE_MAJOR))
$(info GUCFIRMWARE_MINOR=$(GUCFIRMWARE_MINOR))

# ----------------------------------------------------------------------------
# i915 module - copied from drivers/gpu/drm/i915/Makefile
#

# Configuration
EXTRA_CFLAGS += -DCONFIG_PM -DCONFIG_DEBUG_FS -DCONFIG_PNP -DCONFIG_PROC_FS \
				-DCONFIG_MMU_NOTIFIER -DCONFIG_DRM_I915_COMPRESS_ERROR \
				-DCONFIG_COMPAT -DCONFIG_PERF_EVENTS -DCONFIG_PCI_IOV \
				-DCONFIG_X86 -DCONFIG_ACPI -DCONFIG_DRM_FBDEV_EMULATION \
				-DCONFIG_PMIC_OPREGION -DCONFIG_SWIOTLB -DCONFIG_DRM_I915_PXP \
				-DGUC_VF_VERSION_LATEST_MAJOR=$(GUCFIRMWARE_MAJOR) \
				-DGUC_VF_VERSION_LATEST_MINOR=$(GUCFIRMWARE_MINOR) \
				-DEXTRAVERSION_MAJOR=$(EXTRAVERSION_MAJOR) \
				-DEXTRAVERSION_MINOR=$(EXTRAVERSION_MINOR) \
				-D$(EXTRAVERSION_DEFINE) \
				-DLSBRELEASE_MAJOR=$(LSBRELEASE_MAJOR) \
				-DLSBRELEASE_MINOR=$(LSBRELEASE_MINOR) \
				-D$(LSBRELEASE_DEFINE)

KBUILD_MODPOST_WARN = 1

# core driver code
i915-y += i915_driver.o \
	  i915_drm_client.o \
	  i915_config.o \
	  i915_getparam.o \
	  i915_hwmon.o \
	  i915_ioctl.o \
	  i915_irq.o \
	  i915_mitigations.o \
	  i915_module.o \
	  i915_params.o \
	  i915_pci.o \
	  i915_scatterlist.o \
	  i915_suspend.o \
	  i915_switcheroo.o \
	  i915_sysfs.o \
	  i915_utils.o \
	  intel_device_info.o \
	  intel_memory_region.o \
	  intel_pcode.o \
	  intel_pm.o \
	  intel_region_ttm.o \
	  intel_runtime_pm.o \
	  intel_sbi.o \
	  intel_step.o \
	  intel_uncore.o \
	  intel_wakeref.o \
	  vlv_sideband.o \
	  vlv_suspend.o

# core peripheral code
i915-y += \
	soc/intel_dram.o \
	soc/intel_gmch.o \
	soc/intel_pch.o

# core library code
i915-y += \
	i915_memcpy.o \
	i915_mm.o \
	i915_sw_fence.o \
	i915_sw_fence_work.o \
	i915_syncmap.o \
	i915_user_extensions.o

i915-$(CONFIG_COMPAT)   += i915_ioc32.o
i915-$(CONFIG_DEBUG_FS) += \
	i915_debugfs.o \
	i915_debugfs_params.o \
	display/intel_display_debugfs.o \
	display/intel_pipe_crc.o

i915-$(CONFIG_PERF_EVENTS) += i915_pmu.o

# "Graphics Technology" (aka we talk to the gpu)
gt-y += \
	gt/gen2_engine_cs.o \
	gt/gen6_engine_cs.o \
	gt/gen6_ppgtt.o \
	gt/gen7_renderclear.o \
	gt/gen8_engine_cs.o \
	gt/gen8_ppgtt.o \
	gt/intel_breadcrumbs.o \
	gt/intel_context.o \
	gt/intel_context_sseu.o \
	gt/intel_engine_cs.o \
	gt/intel_engine_heartbeat.o \
	gt/intel_engine_pm.o \
	gt/intel_engine_user.o \
	gt/intel_execlists_submission.o \
	gt/intel_ggtt.o \
	gt/intel_ggtt_fencing.o \
	gt/intel_gt.o \
	gt/intel_gt_buffer_pool.o \
	gt/intel_gt_clock_utils.o \
	gt/intel_gt_debugfs.o \
	gt/intel_gt_engines_debugfs.o \
	gt/intel_gt_irq.o \
	gt/intel_gt_mcr.o \
	gt/intel_gt_pm.o \
	gt/intel_gt_pm_debugfs.o \
	gt/intel_gt_pm_irq.o \
	gt/intel_gt_requests.o \
	gt/intel_gt_sysfs.o \
	gt/intel_gt_sysfs_pm.o \
	gt/intel_gtt.o \
	gt/intel_llc.o \
	gt/intel_lrc.o \
	gt/intel_migrate.o \
	gt/intel_mocs.o \
	gt/intel_ppgtt.o \
	gt/intel_rc6.o \
	gt/intel_region_lmem.o \
	gt/intel_renderstate.o \
	gt/intel_reset.o \
	gt/intel_ring.o \
	gt/intel_ring_submission.o \
	gt/intel_rps.o \
	gt/intel_sa_media.o \
	gt/intel_sseu.o \
	gt/intel_sseu_debugfs.o \
	gt/intel_timeline.o \
	gt/intel_tlb.o \
	gt/intel_wopcm.o \
	gt/intel_workarounds.o \
	gt/shmem_utils.o \
	gt/sysfs_engines.o

# x86 intel-gtt module support
gt-$(CONFIG_X86) += gt/intel_ggtt_gmch.o
# autogenerated null render state
gt-y += \
	gt/gen6_renderstate.o \
	gt/gen7_renderstate.o \
	gt/gen8_renderstate.o \
	gt/gen9_renderstate.o
i915-y += $(gt-y)

# GEM (Graphics Execution Management) code
gem-y += \
	gem/i915_gem_busy.o \
	gem/i915_gem_clflush.o \
	gem/i915_gem_context.o \
	gem/i915_gem_create.o \
	gem/i915_gem_dmabuf.o \
	gem/i915_gem_domain.o \
	gem/i915_gem_execbuffer.o \
	gem/i915_gem_internal.o \
	gem/i915_gem_object.o \
	gem/i915_gem_lmem.o \
	gem/i915_gem_mman.o \
	gem/i915_gem_pages.o \
	gem/i915_gem_phys.o \
	gem/i915_gem_pm.o \
	gem/i915_gem_region.o \
	gem/i915_gem_shmem.o \
	gem/i915_gem_shrinker.o \
	gem/i915_gem_stolen.o \
	gem/i915_gem_throttle.o \
	gem/i915_gem_tiling.o \
	gem/i915_gem_ttm.o \
	gem/i915_gem_ttm_move.o \
	gem/i915_gem_ttm_pm.o \
	gem/i915_gem_userptr.o \
	gem/i915_gem_wait.o \
	gem/i915_gemfs.o
i915-y += \
	  $(gem-y) \
	  i915_active.o \
	  i915_cmd_parser.o \
	  i915_deps.o \
	  i915_gem_evict.o \
	  i915_gem_gtt.o \
	  i915_gem_ww.o \
	  i915_gem.o \
	  i915_query.o \
	  i915_request.o \
	  i915_scheduler.o \
	  i915_trace_points.o \
	  i915_ttm_buddy_manager.o \
	  i915_vma.o \
	  i915_vma_resource.o

# general-purpose microcontroller (GuC) support
i915-y += \
	  gt/uc/intel_gsc_fw.o \
	  gt/uc/intel_gsc_proxy.o \
	  gt/uc/intel_gsc_uc.o \
	  gt/uc/intel_gsc_uc_heci_cmd_submit.o \
	  gt/uc/intel_guc.o \
	  gt/uc/intel_guc_ads.o \
	  gt/uc/intel_guc_capture.o \
	  gt/uc/intel_guc_ct.o \
	  gt/uc/intel_guc_debugfs.o \
	  gt/uc/intel_guc_fw.o \
	  gt/uc/intel_guc_hwconfig.o \
	  gt/uc/intel_guc_log.o \
	  gt/uc/intel_guc_log_debugfs.o \
	  gt/uc/intel_guc_rc.o \
	  gt/uc/intel_guc_slpc.o \
	  gt/uc/intel_guc_submission.o \
	  gt/uc/intel_huc.o \
	  gt/uc/intel_huc_debugfs.o \
	  gt/uc/intel_huc_fw.o \
	  gt/uc/intel_uc.o \
	  gt/uc/intel_uc_debugfs.o \
	  gt/uc/intel_uc_fw.o

# graphics system controller (GSC) support
i915-y += gt/intel_gsc.o

# Virtualization support
iov-y += \
	i915_sriov.o \
	i915_sriov_sysfs.o \
	gt/iov/intel_iov.o \
	gt/iov/intel_iov_debugfs.o \
	gt/iov/intel_iov_event.o \
	gt/iov/intel_iov_memirq.o \
	gt/iov/intel_iov_provisioning.o \
	gt/iov/intel_iov_query.o \
	gt/iov/intel_iov_relay.o \
	gt/iov/intel_iov_service.o \
	gt/iov/intel_iov_state.o \
	gt/iov/intel_iov_sysfs.o
i915-y += $(iov-y)

# modesetting core code
i915-y += \
	display/hsw_ips.o \
	display/intel_atomic.o \
	display/intel_atomic_plane.o \
	display/intel_audio.o \
	display/intel_bios.o \
	display/intel_bw.o \
	display/intel_cdclk.o \
	display/intel_color.o \
	display/intel_combo_phy.o \
	display/intel_connector.o \
	display/intel_crtc.o \
	display/intel_crtc_state_dump.o \
	display/intel_cursor.o \
	display/intel_display.o \
	display/intel_display_power.o \
	display/intel_display_power_map.o \
	display/intel_display_power_well.o \
	display/intel_dmc.o \
	display/intel_dpio_phy.o \
	display/intel_dpll.o \
	display/intel_dpll_mgr.o \
	display/intel_dpt.o \
	display/intel_drrs.o \
	display/intel_dsb.o \
	display/intel_fb.o \
	display/intel_fb_pin.o \
	display/intel_fbc.o \
	display/intel_fdi.o \
	display/intel_fifo_underrun.o \
	display/intel_frontbuffer.o \
	display/intel_global_state.o \
	display/intel_hdcp.o \
	display/intel_hotplug.o \
	display/intel_hti.o \
	display/intel_lpe_audio.o \
	display/intel_modeset_verify.o \
	display/intel_modeset_setup.o \
	display/intel_overlay.o \
	display/intel_pch_display.o \
	display/intel_pch_refclk.o \
	display/intel_plane_initial.o \
	display/intel_psr.o \
	display/intel_quirks.o \
	display/intel_sprite.o \
	display/intel_tc.o \
	display/intel_vblank.o \
	display/intel_vga.o \
	display/i9xx_plane.o \
	display/skl_scaler.o \
	display/skl_universal_plane.o \
	display/skl_watermark.o
i915-$(CONFIG_ACPI) += \
	display/intel_acpi.o \
	display/intel_opregion.o
i915-$(CONFIG_DRM_FBDEV_EMULATION) += \
	display/intel_fbdev.o

# modesetting output/encoder code
i915-y += \
	display/dvo_ch7017.o \
	display/dvo_ch7xxx.o \
	display/dvo_ivch.o \
	display/dvo_ns2501.o \
	display/dvo_sil164.o \
	display/dvo_tfp410.o \
	display/g4x_dp.o \
	display/g4x_hdmi.o \
	display/icl_dsi.o \
	display/intel_backlight.o \
	display/intel_crt.o \
	display/intel_cx0_phy.o \
	display/intel_ddi.o \
	display/intel_ddi_buf_trans.o \
	display/intel_display_trace.o \
	display/intel_dkl_phy.o \
	display/intel_dp.o \
	display/intel_dp_aux.o \
	display/intel_dp_aux_backlight.o \
	display/intel_dp_hdcp.o \
	display/intel_dp_link_training.o \
	display/intel_dp_mst.o \
	display/intel_dsi.o \
	display/intel_dsi_dcs_backlight.o \
	display/intel_dsi_vbt.o \
	display/intel_dvo.o \
	display/intel_gmbus.o \
	display/intel_hdmi.o \
	display/intel_lspcon.o \
	display/intel_lvds.o \
	display/intel_panel.o \
	display/intel_pps.o \
	display/intel_qp_tables.o \
	display/intel_sdvo.o \
	display/intel_snps_phy.o \
	display/intel_tv.o \
	display/intel_vdsc.o \
	display/intel_vrr.o \
	display/vlv_dsi.o \
	display/vlv_dsi_pll.o

i915-y += i915_perf.o

# Protected execution platform (PXP) support
i915-y += \
	pxp/intel_pxp.o \
	pxp/intel_pxp_tee.o \
	pxp/intel_pxp_huc.o

i915-$(CONFIG_DRM_I915_PXP) += \
	pxp/intel_pxp_cmd.o \
	pxp/intel_pxp_debugfs.o \
	pxp/intel_pxp_gsccs.o \
	pxp/intel_pxp_irq.o \
	pxp/intel_pxp_pm.o \
	pxp/intel_pxp_session.o

# Post-mortem debug and GPU hang state capture
i915-$(CONFIG_DRM_I915_CAPTURE_ERROR) += i915_gpu_error.o
i915-$(CONFIG_DRM_I915_SELFTEST) += \
	gem/selftests/i915_gem_client_blt.o \
	gem/selftests/igt_gem_utils.o \
	selftests/intel_scheduler_helpers.o \
	selftests/i915_random.o \
	selftests/i915_selftest.o \
	selftests/igt_atomic.o \
	selftests/igt_flush_test.o \
	selftests/igt_live_test.o \
	selftests/igt_mmap.o \
	selftests/igt_reset.o \
	selftests/igt_spinner.o \
	selftests/librapl.o

# virtual gpu code
i915-y += i915_vgpu.o

i915-$(CONFIG_DRM_I915_GVT) += \
	intel_gvt.o \
	intel_gvt_mmio_table.o

obj-$(CONFIG_DRM_I915)           += i915.o

CFLAGS_i915_trace_points.o := -I$(KBUILD_EXTMOD)/drivers/gpu/drm/i915


i915-y := $(addprefix drivers/gpu/drm/i915/,$(i915-y))

# ----------------------------------------------------------------------------
# common to all modules
#

# This prioritises the DKMS package include directories over the kernel headers
# allowing us to override header files where the source versions have extra
# structs and declarations and so forth that we need for the backport to build.

LINUXINCLUDE := \
    -I$(KBUILD_EXTMOD)/include \
    -I$(KBUILD_EXTMOD)/include/trace \
    -I$(KBUILD_EXTMOD)/drivers/gpu/drm/i915 \
    $(LINUXINCLUDE)

obj-m := i915.o

.PHONY: default clean modules load unload install patch
