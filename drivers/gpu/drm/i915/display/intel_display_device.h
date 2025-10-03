/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEVICE_H__
#define __INTEL_DISPLAY_DEVICE_H__

#include <linux/bitops.h>
#include <linux/types.h>

#include "intel_display_conversion.h"
#include "intel_display_limits.h"

struct drm_printer;
struct intel_display;
struct pci_dev;

/*
 * Display platforms and subplatforms. Keep platforms in display version based
 * order, chronological order within a version, and subplatforms next to the
 * platform.
 */
#define INTEL_DISPLAY_PLATFORMS(func) \
	/* Platform group aliases */ \
	func(g4x) /* g45 and gm45 */ \
	func(mobile) /* mobile platforms */ \
	func(dgfx) /* discrete graphics */ \
	/* Display ver 2 */ \
	func(i830) \
	func(i845g) \
	func(i85x) \
	func(i865g) \
	/* Display ver 3 */ \
	func(i915g) \
	func(i915gm) \
	func(i945g) \
	func(i945gm) \
	func(g33) \
	func(pineview) \
	/* Display ver 4 */ \
	func(i965g) \
	func(i965gm) \
	func(g45) \
	func(gm45) \
	/* Display ver 5 */ \
	func(ironlake) \
	/* Display ver 6 */ \
	func(sandybridge) \
	/* Display ver 7 */ \
	func(ivybridge) \
	func(valleyview) \
	func(haswell) \
	func(haswell_ult) \
	func(haswell_ulx) \
	/* Display ver 8 */ \
	func(broadwell) \
	func(broadwell_ult) \
	func(broadwell_ulx) \
	func(cherryview) \
	/* Display ver 9 */ \
	func(skylake) \
	func(skylake_ult) \
	func(skylake_ulx) \
	func(broxton) \
	func(kabylake) \
	func(kabylake_ult) \
	func(kabylake_ulx) \
	func(geminilake) \
	func(coffeelake) \
	func(coffeelake_ult) \
	func(coffeelake_ulx) \
	func(cometlake) \
	func(cometlake_ult) \
	func(cometlake_ulx) \
	/* Display ver 11 */ \
	func(icelake) \
	func(icelake_port_f) \
	func(jasperlake) \
	func(elkhartlake) \
	/* Display ver 12 */ \
	func(tigerlake) \
	func(tigerlake_uy) \
	func(rocketlake) \
	func(dg1) \
	func(alderlake_s) \
	func(alderlake_s_raptorlake_s) \
	/* Display ver 13 */ \
	func(alderlake_p) \
	func(alderlake_p_alderlake_n) \
	func(alderlake_p_raptorlake_p) \
	func(alderlake_p_raptorlake_u) \
	func(dg2) \
	func(dg2_g10) \
	func(dg2_g11) \
	func(dg2_g12) \
	/* Display ver 14 (based on GMD ID) */ \
	func(meteorlake) \
	func(meteorlake_u) \
	/* Display ver 20 (based on GMD ID) */ \
	func(lunarlake) \
	/* Display ver 14.1 (based on GMD ID) */ \
	func(battlemage) \
	/* Display ver 30 (based on GMD ID) */ \
	func(pantherlake)

#define __MEMBER(name) unsigned long name:1;
#define __COUNT(x) 1 +

#define __NUM_PLATFORMS (INTEL_DISPLAY_PLATFORMS(__COUNT) 0)

struct intel_display_platforms {
	union {
		struct {
			INTEL_DISPLAY_PLATFORMS(__MEMBER);
		};
		DECLARE_BITMAP(bitmap, __NUM_PLATFORMS);
	};
};

#undef __MEMBER
#undef __COUNT
#undef __NUM_PLATFORMS

#define DEV_INFO_DISPLAY_FOR_EACH_FLAG(func) \
	/* Keep in alphabetical order */ \
	func(cursor_needs_physical); \
	func(has_cdclk_crawl); \
	func(has_cdclk_squash); \
	func(has_ddi); \
	func(has_dp_mst); \
	func(has_dsb); \
	func(has_fpga_dbg); \
	func(has_gmch); \
	func(has_hotplug); \
	func(has_hti); \
	func(has_ipc); \
	func(has_overlay); \
	func(has_psr); \
	func(has_psr_hw_tracking); \
	func(overlay_needs_physical); \
	func(supports_tv);

#define HAS_4TILE(__display)		((__display)->platform.dg2 || DISPLAY_VER(__display) >= 14)
#define HAS_ASYNC_FLIPS(__display)	(DISPLAY_VER(__display) >= 5)
#define HAS_AS_SDP(__display)		(DISPLAY_VER(__display) >= 13)
#define HAS_BIGJOINER(__display)	(DISPLAY_VER(__display) >= 11 && HAS_DSC(__display))
#define HAS_CDCLK_CRAWL(__display)	(DISPLAY_INFO(__display)->has_cdclk_crawl)
#define HAS_CDCLK_SQUASH(__display)	(DISPLAY_INFO(__display)->has_cdclk_squash)
#define HAS_CMRR(__display)		(DISPLAY_VER(__display) >= 20)
#define HAS_CMTG(__display)		(!(__display)->platform.dg2 && DISPLAY_VER(__display) >= 13)
#define HAS_CUR_FBC(__display)		(!HAS_GMCH(__display) && IS_DISPLAY_VER(__display, 7, 13))
#define HAS_D12_PLANE_MINIMIZATION(__display)	((__display)->platform.rocketlake || (__display)->platform.alderlake_s)
#define HAS_DBUF_OVERLAP_DETECTION(__display)	(DISPLAY_RUNTIME_INFO(__display)->has_dbuf_overlap_detection)
#define HAS_DDI(__display)		(DISPLAY_INFO(__display)->has_ddi)
#define HAS_DISPLAY(__display)		(DISPLAY_RUNTIME_INFO(__display)->pipe_mask != 0)
#define HAS_DMC(__display)		(DISPLAY_RUNTIME_INFO(__display)->has_dmc)
#define HAS_DMC_WAKELOCK(__display)	(DISPLAY_VER(__display) >= 20)
#define HAS_DOUBLE_BUFFERED_M_N(__display)	(DISPLAY_VER(__display) >= 9 || (__display)->platform.broadwell)
#define HAS_DOUBLE_BUFFERED_LUT(__display)	(DISPLAY_VER(__display) >= 30)
#define HAS_DOUBLE_WIDE(__display)	(DISPLAY_VER(__display) < 4)
#define HAS_DP20(__display)		((__display)->platform.dg2 || DISPLAY_VER(__display) >= 14)
#define HAS_DPT(__display)		(DISPLAY_VER(__display) >= 13)
#define HAS_DP_MST(__display)		(DISPLAY_INFO(__display)->has_dp_mst)
#define HAS_DSB(__display)		(DISPLAY_INFO(__display)->has_dsb)
#define HAS_DSC(__display)		(DISPLAY_RUNTIME_INFO(__display)->has_dsc)
#define HAS_DSC_3ENGINES(__display)	(DISPLAY_VERx100(__display) == 1401 && HAS_DSC(__display))
#define HAS_DSC_MST(__display)		(DISPLAY_VER(__display) >= 12 && HAS_DSC(__display))
#define HAS_FBC(__display)		(DISPLAY_RUNTIME_INFO(__display)->fbc_mask != 0)
#define HAS_FBC_DIRTY_RECT(__display)	(DISPLAY_VER(__display) >= 30)
#define HAS_FPGA_DBG_UNCLAIMED(__display)	(DISPLAY_INFO(__display)->has_fpga_dbg)
#define HAS_FW_BLC(__display)		(DISPLAY_VER(__display) >= 3)
#define HAS_GMBUS_BURST_READ(__display)	(DISPLAY_VER(__display) >= 10 || (__display)->platform.kabylake)
#define HAS_GMBUS_IRQ(__display)	(DISPLAY_VER(__display) >= 4)
#define HAS_GMCH(__display)		(DISPLAY_INFO(__display)->has_gmch)
#define HAS_FDI(__display)		(IS_DISPLAY_VER((__display), 5, 8) && !HAS_GMCH(__display))
#define HAS_HOTPLUG(__display)		(DISPLAY_INFO(__display)->has_hotplug)
#define HAS_HW_SAGV_WM(__display)	(DISPLAY_VER(__display) >= 13 && !(__display)->platform.dgfx)
#define HAS_IPC(__display)		(DISPLAY_INFO(__display)->has_ipc)
#define HAS_IPS(__display)		((__display)->platform.haswell_ult || (__display)->platform.broadwell)
#define HAS_LRR(__display)		(DISPLAY_VER(__display) >= 12)
#define HAS_LSPCON(__display)		(IS_DISPLAY_VER(__display, 9, 10))
#define HAS_MBUS_JOINING(__display)	((__display)->platform.alderlake_p || DISPLAY_VER(__display) >= 14)
#define HAS_MSO(__display)		(DISPLAY_VER(__display) >= 12)
#define HAS_OVERLAY(__display)		(DISPLAY_INFO(__display)->has_overlay)
#define HAS_PIPEDMC(__display)		(DISPLAY_VER(__display) >= 12)
#define HAS_PSR(__display)		(DISPLAY_INFO(__display)->has_psr)
#define HAS_PSR_HW_TRACKING(__display)	(DISPLAY_INFO(__display)->has_psr_hw_tracking)
#define HAS_PSR2_SEL_FETCH(__display)	(DISPLAY_VER(__display) >= 12)
#define HAS_SAGV(__display)		(DISPLAY_VER(__display) >= 9 && \
					 !(__display)->platform.broxton && !(__display)->platform.geminilake)
#define HAS_TRANSCODER(__display, trans)	((DISPLAY_RUNTIME_INFO(__display)->cpu_transcoder_mask & \
						  BIT(trans)) != 0)
#define HAS_UNCOMPRESSED_JOINER(__display)	(DISPLAY_VER(__display) >= 13)
#define HAS_ULTRAJOINER(__display)	(((__display)->platform.dgfx && \
					  DISPLAY_VER(__display) == 14) && HAS_DSC(__display))
#define HAS_VRR(__display)		(DISPLAY_VER(__display) >= 11)
#define INTEL_NUM_PIPES(__display)	(hweight8(DISPLAY_RUNTIME_INFO(__display)->pipe_mask))
#define OVERLAY_NEEDS_PHYSICAL(__display)	(DISPLAY_INFO(__display)->overlay_needs_physical)
#define SUPPORTS_TV(__display)		(DISPLAY_INFO(__display)->supports_tv)

/* Check that device has a display IP version within the specific range. */
#define IS_DISPLAY_VERx100(__display, from, until) ( \
	BUILD_BUG_ON_ZERO((from) < 200) + \
	(DISPLAY_VERx100(__display) >= (from) && \
	 DISPLAY_VERx100(__display) <= (until)))

/*
 * Check if a device has a specific IP version as well as a stepping within the
 * specified range [from, until).  The lower bound is inclusive, the upper
 * bound is exclusive.  The most common use-case of this macro is for checking
 * bounds for workarounds, which usually have a stepping ("from") at which the
 * hardware issue is first present and another stepping ("until") at which a
 * hardware fix is present and the software workaround is no longer necessary.
 * E.g.,
 *
 *    IS_DISPLAY_VERx100_STEP(display, 1400, STEP_A0, STEP_B2)
 *    IS_DISPLAY_VERx100_STEP(display, 1400, STEP_C0, STEP_FOREVER)
 *
 * "STEP_FOREVER" can be passed as "until" for workarounds that have no upper
 * stepping bound for the specified IP version.
 */
#define IS_DISPLAY_VERx100_STEP(__display, ipver, from, until) \
	(IS_DISPLAY_VERx100((__display), (ipver), (ipver)) && \
	 IS_DISPLAY_STEP((__display), (from), (until)))

#define DISPLAY_INFO(__display)		(__to_intel_display(__display)->info.__device_info)
#define DISPLAY_RUNTIME_INFO(__display)	(&__to_intel_display(__display)->info.__runtime_info)

#define DISPLAY_VER(__display)		(DISPLAY_RUNTIME_INFO(__display)->ip.ver)
#define DISPLAY_VERx100(__display)	(DISPLAY_RUNTIME_INFO(__display)->ip.ver * 100 + \
					 DISPLAY_RUNTIME_INFO(__display)->ip.rel)
#define IS_DISPLAY_VER(__display, from, until) \
	(DISPLAY_VER(__display) >= (from) && DISPLAY_VER(__display) <= (until))

#define INTEL_DISPLAY_STEP(__display)	(DISPLAY_RUNTIME_INFO(__display)->step)

#define IS_DISPLAY_STEP(__display, since, until) \
	(drm_WARN_ON(__to_intel_display(__display)->drm, INTEL_DISPLAY_STEP(__display) == STEP_NONE), \
	 INTEL_DISPLAY_STEP(__display) >= (since) && INTEL_DISPLAY_STEP(__display) < (until))

#define ARLS_HOST_BRIDGE_PCI_ID1 0x7D1C
#define ARLS_HOST_BRIDGE_PCI_ID2 0x7D2D
#define ARLS_HOST_BRIDGE_PCI_ID3 0x7D2E
#define ARLS_HOST_BRIDGE_PCI_ID4 0x7D2F

#define IS_ARROWLAKE_S_BY_HOST_BRIDGE_ID(id)  \
	(((id) == ARLS_HOST_BRIDGE_PCI_ID1) || \
	 ((id) == ARLS_HOST_BRIDGE_PCI_ID2) || \
	 ((id) == ARLS_HOST_BRIDGE_PCI_ID3) || \
	 ((id) == ARLS_HOST_BRIDGE_PCI_ID4))

struct intel_display_runtime_info {
	struct intel_display_ip_ver {
		u16 ver;
		u16 rel;
		u16 step; /* hardware */
	} ip;
	int step; /* symbolic */

	u32 rawclk_freq;

	u8 pipe_mask;
	u8 cpu_transcoder_mask;
	u16 port_mask;

	u8 num_sprites[I915_MAX_PIPES];
	u8 num_scalers[I915_MAX_PIPES];

	u8 fbc_mask;

	bool has_hdcp;
	bool has_dmc;
	bool has_dsc;
	bool edp_typec_support;
	bool has_dbuf_overlap_detection;
};

struct intel_display_device_info {
	/* Initial runtime info. */
	const struct intel_display_runtime_info __runtime_defaults;

	u8 abox_mask;

	struct {
		u16 size; /* in blocks */
		u8 slice_mask;
	} dbuf;

#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_DISPLAY_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG

	/* Global register offset for the display engine */
	u32 mmio_offset;

	/* Register offsets for the various display pipes and transcoders */
	u32 pipe_offsets[I915_MAX_TRANSCODERS];
	u32 trans_offsets[I915_MAX_TRANSCODERS];
	u32 cursor_offsets[I915_MAX_PIPES];

	struct {
		u32 degamma_lut_size;
		u32 gamma_lut_size;
		u32 degamma_lut_tests;
		u32 gamma_lut_tests;
	} color;
};

bool intel_display_device_enabled(struct intel_display *display);
struct intel_display *intel_display_device_probe(struct pci_dev *pdev);
void intel_display_device_remove(struct intel_display *display);
void intel_display_device_info_runtime_init(struct intel_display *display);

void intel_display_device_info_print(const struct intel_display_device_info *info,
				     const struct intel_display_runtime_info *runtime,
				     struct drm_printer *p);

#endif
