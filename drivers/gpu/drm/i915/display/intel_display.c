/*
 * Copyright © 2006-2007 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */

#include <linux/dma-resv.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_tunnel.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_vblank.h>

#include "g4x_dp.h"
#include "g4x_hdmi.h"
#include "hsw_ips.h"
#include "i915_config.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "i915_utils.h"
#include "i9xx_plane.h"
#include "i9xx_plane_regs.h"
#include "i9xx_wm.h"
#include "intel_alpm.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_bo.h"
#include "intel_bw.h"
#include "intel_cdclk.h"
#include "intel_clock_gating.h"
#include "intel_color.h"
#include "intel_crt.h"
#include "intel_crtc.h"
#include "intel_crtc_state_dump.h"
#include "intel_cursor.h"
#include "intel_cursor_regs.h"
#include "intel_cx0_phy.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_driver.h"
#include "intel_display_power.h"
#include "intel_display_regs.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dp_tunnel.h"
#include "intel_dpll.h"
#include "intel_dpll_mgr.h"
#include "intel_dpt.h"
#include "intel_dpt_common.h"
#include "intel_drrs.h"
#include "intel_dsb.h"
#include "intel_dsi.h"
#include "intel_dvo.h"
#include "intel_fb.h"
#include "intel_fbc.h"
#include "intel_fdi.h"
#include "intel_fifo_underrun.h"
#include "intel_flipq.h"
#include "intel_frontbuffer.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_link_bw.h"
#include "intel_lvds.h"
#include "intel_lvds_regs.h"
#include "intel_modeset_setup.h"
#include "intel_modeset_verify.h"
#include "intel_overlay.h"
#include "intel_panel.h"
#include "intel_pch_display.h"
#include "intel_pch_refclk.h"
#include "intel_pfit.h"
#include "intel_pipe_crc.h"
#include "intel_plane.h"
#include "intel_plane_initial.h"
#include "intel_pmdemand.h"
#include "intel_pps.h"
#include "intel_psr.h"
#include "intel_psr_regs.h"
#include "intel_sdvo.h"
#include "intel_snps_phy.h"
#include "intel_tc.h"
#include "intel_tdf.h"
#include "intel_tv.h"
#include "intel_vblank.h"
#include "intel_vdsc.h"
#include "intel_vdsc_regs.h"
#include "intel_vga.h"
#include "intel_vrr.h"
#include "intel_wm.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"
#include "skl_watermark.h"
#include "vlv_dpio_phy_regs.h"
#include "vlv_dsi.h"
#include "vlv_dsi_pll.h"
#include "vlv_dsi_regs.h"
#include "vlv_sideband.h"

static void intel_set_transcoder_timings(const struct intel_crtc_state *crtc_state);
static void intel_set_pipe_src_size(const struct intel_crtc_state *crtc_state);
static void hsw_set_transconf(const struct intel_crtc_state *crtc_state);
static void bdw_set_pipe_misc(struct intel_dsb *dsb,
			      const struct intel_crtc_state *crtc_state);

/* returns HPLL frequency in kHz */
int vlv_get_hpll_vco(struct drm_device *drm)
{
	int hpll_freq, vco_freq[] = { 800, 1600, 2000, 2400 };

	/* Obtain SKU information */
	hpll_freq = vlv_cck_read(drm, CCK_FUSE_REG) &
		CCK_FUSE_HPLL_FREQ_MASK;

	return vco_freq[hpll_freq] * 1000;
}

int vlv_get_cck_clock(struct drm_device *drm,
		      const char *name, u32 reg, int ref_freq)
{
	u32 val;
	int divider;

	val = vlv_cck_read(drm, reg);
	divider = val & CCK_FREQUENCY_VALUES;

	drm_WARN(drm, (val & CCK_FREQUENCY_STATUS) !=
		 (divider << CCK_FREQUENCY_STATUS_SHIFT),
		 "%s change in progress\n", name);

	return DIV_ROUND_CLOSEST(ref_freq << 1, divider + 1);
}

int vlv_get_cck_clock_hpll(struct drm_device *drm,
			   const char *name, u32 reg)
{
	struct drm_i915_private *dev_priv = to_i915(drm);
	int hpll;

	vlv_cck_get(drm);

	if (dev_priv->hpll_freq == 0)
		dev_priv->hpll_freq = vlv_get_hpll_vco(drm);

	hpll = vlv_get_cck_clock(drm, name, reg, dev_priv->hpll_freq);

	vlv_cck_put(drm);

	return hpll;
}

void intel_update_czclk(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	if (!display->platform.valleyview && !display->platform.cherryview)
		return;

	dev_priv->czclk_freq = vlv_get_cck_clock_hpll(display->drm, "czclk",
						      CCK_CZ_CLOCK_CONTROL);

	drm_dbg_kms(display->drm, "CZ clock rate: %d kHz\n", dev_priv->czclk_freq);
}

static bool is_hdr_mode(const struct intel_crtc_state *crtc_state)
{
	return (crtc_state->active_planes &
		~(icl_hdr_plane_mask() | BIT(PLANE_CURSOR))) == 0;
}

/* WA Display #0827: Gen9:all */
static void
skl_wa_827(struct intel_display *display, enum pipe pipe, bool enable)
{
	intel_de_rmw(display, CLKGATE_DIS_PSL(pipe),
		     DUPS1_GATING_DIS | DUPS2_GATING_DIS,
		     enable ? DUPS1_GATING_DIS | DUPS2_GATING_DIS : 0);
}

/* Wa_2006604312:icl,ehl */
static void
icl_wa_scalerclkgating(struct intel_display *display, enum pipe pipe,
		       bool enable)
{
	intel_de_rmw(display, CLKGATE_DIS_PSL(pipe),
		     DPFR_GATING_DIS,
		     enable ? DPFR_GATING_DIS : 0);
}

/* Wa_1604331009:icl,jsl,ehl */
static void
icl_wa_cursorclkgating(struct intel_display *display, enum pipe pipe,
		       bool enable)
{
	intel_de_rmw(display, CLKGATE_DIS_PSL(pipe),
		     CURSOR_GATING_DIS,
		     enable ? CURSOR_GATING_DIS : 0);
}

static bool
is_trans_port_sync_slave(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->master_transcoder != INVALID_TRANSCODER;
}

bool
is_trans_port_sync_master(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->sync_mode_slaves_mask != 0;
}

bool
is_trans_port_sync_mode(const struct intel_crtc_state *crtc_state)
{
	return is_trans_port_sync_master(crtc_state) ||
		is_trans_port_sync_slave(crtc_state);
}

static enum pipe joiner_primary_pipe(const struct intel_crtc_state *crtc_state)
{
	return ffs(crtc_state->joiner_pipes) - 1;
}

/*
 * The following helper functions, despite being named for bigjoiner,
 * are applicable to both bigjoiner and uncompressed joiner configurations.
 */
static bool is_bigjoiner(const struct intel_crtc_state *crtc_state)
{
	return hweight8(crtc_state->joiner_pipes) >= 2;
}

static u8 bigjoiner_primary_pipes(const struct intel_crtc_state *crtc_state)
{
	if (!is_bigjoiner(crtc_state))
		return 0;

	return crtc_state->joiner_pipes & (0b01010101 << joiner_primary_pipe(crtc_state));
}

static unsigned int bigjoiner_secondary_pipes(const struct intel_crtc_state *crtc_state)
{
	if (!is_bigjoiner(crtc_state))
		return 0;

	return crtc_state->joiner_pipes & (0b10101010 << joiner_primary_pipe(crtc_state));
}

bool intel_crtc_is_bigjoiner_primary(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!is_bigjoiner(crtc_state))
		return false;

	return BIT(crtc->pipe) & bigjoiner_primary_pipes(crtc_state);
}

bool intel_crtc_is_bigjoiner_secondary(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!is_bigjoiner(crtc_state))
		return false;

	return BIT(crtc->pipe) & bigjoiner_secondary_pipes(crtc_state);
}

u8 _intel_modeset_primary_pipes(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!is_bigjoiner(crtc_state))
		return BIT(crtc->pipe);

	return bigjoiner_primary_pipes(crtc_state);
}

u8 _intel_modeset_secondary_pipes(const struct intel_crtc_state *crtc_state)
{
	return bigjoiner_secondary_pipes(crtc_state);
}

bool intel_crtc_is_ultrajoiner(const struct intel_crtc_state *crtc_state)
{
	return intel_crtc_num_joined_pipes(crtc_state) >= 4;
}

static u8 ultrajoiner_primary_pipes(const struct intel_crtc_state *crtc_state)
{
	if (!intel_crtc_is_ultrajoiner(crtc_state))
		return 0;

	return crtc_state->joiner_pipes & (0b00010001 << joiner_primary_pipe(crtc_state));
}

bool intel_crtc_is_ultrajoiner_primary(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	return intel_crtc_is_ultrajoiner(crtc_state) &&
	       BIT(crtc->pipe) & ultrajoiner_primary_pipes(crtc_state);
}

/*
 * The ultrajoiner enable bit doesn't seem to follow primary/secondary logic or
 * any other logic, so lets just add helper function to
 * at least hide this hassle..
 */
static u8 ultrajoiner_enable_pipes(const struct intel_crtc_state *crtc_state)
{
	if (!intel_crtc_is_ultrajoiner(crtc_state))
		return 0;

	return crtc_state->joiner_pipes & (0b01110111 << joiner_primary_pipe(crtc_state));
}

bool intel_crtc_ultrajoiner_enable_needed(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	return intel_crtc_is_ultrajoiner(crtc_state) &&
	       BIT(crtc->pipe) & ultrajoiner_enable_pipes(crtc_state);
}

u8 intel_crtc_joiner_secondary_pipes(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->joiner_pipes)
		return crtc_state->joiner_pipes & ~BIT(joiner_primary_pipe(crtc_state));
	else
		return 0;
}

bool intel_crtc_is_joiner_secondary(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	return crtc_state->joiner_pipes &&
		crtc->pipe != joiner_primary_pipe(crtc_state);
}

bool intel_crtc_is_joiner_primary(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	return crtc_state->joiner_pipes &&
		crtc->pipe == joiner_primary_pipe(crtc_state);
}

int intel_crtc_num_joined_pipes(const struct intel_crtc_state *crtc_state)
{
	return hweight8(intel_crtc_joined_pipe_mask(crtc_state));
}

u8 intel_crtc_joined_pipe_mask(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	return BIT(crtc->pipe) | crtc_state->joiner_pipes;
}

struct intel_crtc *intel_primary_crtc(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (intel_crtc_is_joiner_secondary(crtc_state))
		return intel_crtc_for_pipe(display, joiner_primary_pipe(crtc_state));
	else
		return to_intel_crtc(crtc_state->uapi.crtc);
}

static void
intel_wait_for_pipe_off(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);

	if (DISPLAY_VER(display) >= 4) {
		enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

		/* Wait for the Pipe State to go off */
		if (intel_de_wait_for_clear(display, TRANSCONF(display, cpu_transcoder),
					    TRANSCONF_STATE_ENABLE, 100))
			drm_WARN(display->drm, 1, "pipe_off wait timed out\n");
	} else {
		intel_wait_for_pipe_scanline_stopped(crtc);
	}
}

void assert_transcoder(struct intel_display *display,
		       enum transcoder cpu_transcoder, bool state)
{
	bool cur_state;
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;

	/* we keep both pipes enabled on 830 */
	if (display->platform.i830)
		state = true;

	power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);
	wakeref = intel_display_power_get_if_enabled(display, power_domain);
	if (wakeref) {
		u32 val = intel_de_read(display,
					TRANSCONF(display, cpu_transcoder));
		cur_state = !!(val & TRANSCONF_ENABLE);

		intel_display_power_put(display, power_domain, wakeref);
	} else {
		cur_state = false;
	}

	INTEL_DISPLAY_STATE_WARN(display, cur_state != state,
				 "transcoder %s assertion failure (expected %s, current %s)\n",
				 transcoder_name(cpu_transcoder), str_on_off(state),
				 str_on_off(cur_state));
}

static void assert_plane(struct intel_plane *plane, bool state)
{
	struct intel_display *display = to_intel_display(plane->base.dev);
	enum pipe pipe;
	bool cur_state;

	cur_state = plane->get_hw_state(plane, &pipe);

	INTEL_DISPLAY_STATE_WARN(display, cur_state != state,
				 "%s assertion failure (expected %s, current %s)\n",
				 plane->base.name, str_on_off(state),
				 str_on_off(cur_state));
}

#define assert_plane_enabled(p) assert_plane(p, true)
#define assert_plane_disabled(p) assert_plane(p, false)

static void assert_planes_disabled(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_plane *plane;

	for_each_intel_plane_on_crtc(display->drm, crtc, plane)
		assert_plane_disabled(plane);
}

void intel_enable_transcoder(const struct intel_crtc_state *new_crtc_state)
{
	struct intel_display *display = to_intel_display(new_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = new_crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	u32 val;

	drm_dbg_kms(display->drm, "enabling pipe %c\n", pipe_name(pipe));

	assert_planes_disabled(crtc);

	/*
	 * A pipe without a PLL won't actually be able to drive bits from
	 * a plane.  On ILK+ the pipe PLLs are integrated, so we don't
	 * need the check.
	 */
	if (HAS_GMCH(display)) {
		if (intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI))
			assert_dsi_pll_enabled(display);
		else
			assert_pll_enabled(display, pipe);
	} else {
		if (new_crtc_state->has_pch_encoder) {
			/* if driving the PCH, we need FDI enabled */
			assert_fdi_rx_pll_enabled(display,
						  intel_crtc_pch_transcoder(crtc));
			assert_fdi_tx_pll_enabled(display,
						  (enum pipe) cpu_transcoder);
		}
		/* FIXME: assert CPU port conditions for SNB+ */
	}

	/* Wa_22012358565:adl-p */
	if (DISPLAY_VER(display) == 13)
		intel_de_rmw(display, PIPE_ARB_CTL(display, pipe),
			     0, PIPE_ARB_USE_PROG_SLOTS);

	if (DISPLAY_VER(display) >= 14) {
		u32 clear = DP_DSC_INSERT_SF_AT_EOL_WA;
		u32 set = 0;

		if (DISPLAY_VER(display) == 14)
			set |= DP_FEC_BS_JITTER_WA;

		intel_de_rmw(display, CHICKEN_TRANS(display, cpu_transcoder),
			     clear, set);
	}

	val = intel_de_read(display, TRANSCONF(display, cpu_transcoder));
	if (val & TRANSCONF_ENABLE) {
		/* we keep both pipes enabled on 830 */
		drm_WARN_ON(display->drm, !display->platform.i830);
		return;
	}

	/* Wa_1409098942:adlp+ */
	if (DISPLAY_VER(display) >= 13 &&
	    new_crtc_state->dsc.compression_enable) {
		val &= ~TRANSCONF_PIXEL_COUNT_SCALING_MASK;
		val |= REG_FIELD_PREP(TRANSCONF_PIXEL_COUNT_SCALING_MASK,
				      TRANSCONF_PIXEL_COUNT_SCALING_X4);
	}

	intel_de_write(display, TRANSCONF(display, cpu_transcoder),
		       val | TRANSCONF_ENABLE);
	intel_de_posting_read(display, TRANSCONF(display, cpu_transcoder));

	/*
	 * Until the pipe starts PIPEDSL reads will return a stale value,
	 * which causes an apparent vblank timestamp jump when PIPEDSL
	 * resets to its proper value. That also messes up the frame count
	 * when it's derived from the timestamps. So let's wait for the
	 * pipe to start properly before we call drm_crtc_vblank_on()
	 */
	if (intel_crtc_max_vblank_count(new_crtc_state) == 0)
		intel_wait_for_pipe_scanline_moving(crtc);
}

void intel_disable_transcoder(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	u32 val;

	drm_dbg_kms(display->drm, "disabling pipe %c\n", pipe_name(pipe));

	/*
	 * Make sure planes won't keep trying to pump pixels to us,
	 * or we might hang the display.
	 */
	assert_planes_disabled(crtc);

	val = intel_de_read(display, TRANSCONF(display, cpu_transcoder));
	if ((val & TRANSCONF_ENABLE) == 0)
		return;

	/*
	 * Double wide has implications for planes
	 * so best keep it disabled when not needed.
	 */
	if (old_crtc_state->double_wide)
		val &= ~TRANSCONF_DOUBLE_WIDE;

	/* Don't disable pipe or pipe PLLs if needed */
	if (!display->platform.i830)
		val &= ~TRANSCONF_ENABLE;

	/* Wa_1409098942:adlp+ */
	if (DISPLAY_VER(display) >= 13 &&
	    old_crtc_state->dsc.compression_enable)
		val &= ~TRANSCONF_PIXEL_COUNT_SCALING_MASK;

	intel_de_write(display, TRANSCONF(display, cpu_transcoder), val);

	if (DISPLAY_VER(display) >= 12)
		intel_de_rmw(display, CHICKEN_TRANS(display, cpu_transcoder),
			     FECSTALL_DIS_DPTSTREAM_DPTTG, 0);

	if ((val & TRANSCONF_ENABLE) == 0)
		intel_wait_for_pipe_off(old_crtc_state);
}

u32 intel_plane_fb_max_stride(struct drm_device *drm,
			      u32 pixel_format, u64 modifier)
{
	struct intel_display *display = to_intel_display(drm);
	struct intel_crtc *crtc;
	struct intel_plane *plane;

	if (!HAS_DISPLAY(display))
		return 0;

	/*
	 * We assume the primary plane for pipe A has
	 * the highest stride limits of them all,
	 * if in case pipe A is disabled, use the first pipe from pipe_mask.
	 */
	crtc = intel_first_crtc(display);
	if (!crtc)
		return 0;

	plane = to_intel_plane(crtc->base.primary);

	return plane->max_stride(plane, pixel_format, modifier,
				 DRM_MODE_ROTATE_0);
}

void intel_set_plane_visible(struct intel_crtc_state *crtc_state,
			     struct intel_plane_state *plane_state,
			     bool visible)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

	plane_state->uapi.visible = visible;

	if (visible)
		crtc_state->uapi.plane_mask |= drm_plane_mask(&plane->base);
	else
		crtc_state->uapi.plane_mask &= ~drm_plane_mask(&plane->base);
}

void intel_plane_fixup_bitmasks(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct drm_plane *plane;

	/*
	 * Active_planes aliases if multiple "primary" or cursor planes
	 * have been used on the same (or wrong) pipe. plane_mask uses
	 * unique ids, hence we can use that to reconstruct active_planes.
	 */
	crtc_state->enabled_planes = 0;
	crtc_state->active_planes = 0;

	drm_for_each_plane_mask(plane, display->drm,
				crtc_state->uapi.plane_mask) {
		crtc_state->enabled_planes |= BIT(to_intel_plane(plane)->id);
		crtc_state->active_planes |= BIT(to_intel_plane(plane)->id);
	}
}

void intel_plane_disable_noatomic(struct intel_crtc *crtc,
				  struct intel_plane *plane)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_plane_state *plane_state =
		to_intel_plane_state(plane->base.state);

	drm_dbg_kms(display->drm,
		    "Disabling [PLANE:%d:%s] on [CRTC:%d:%s]\n",
		    plane->base.base.id, plane->base.name,
		    crtc->base.base.id, crtc->base.name);

	intel_plane_set_invisible(crtc_state, plane_state);
	intel_set_plane_visible(crtc_state, plane_state, false);
	intel_plane_fixup_bitmasks(crtc_state);

	skl_wm_plane_disable_noatomic(crtc, plane);

	if ((crtc_state->active_planes & ~BIT(PLANE_CURSOR)) == 0 &&
	    hsw_ips_disable(crtc_state)) {
		crtc_state->ips_enabled = false;
		intel_plane_initial_vblank_wait(crtc);
	}

	/*
	 * Vblank time updates from the shadow to live plane control register
	 * are blocked if the memory self-refresh mode is active at that
	 * moment. So to make sure the plane gets truly disabled, disable
	 * first the self-refresh mode. The self-refresh enable bit in turn
	 * will be checked/applied by the HW only at the next frame start
	 * event which is after the vblank start event, so we need to have a
	 * wait-for-vblank between disabling the plane and the pipe.
	 */
	if (HAS_GMCH(display) &&
	    intel_set_memory_cxsr(display, false))
		intel_plane_initial_vblank_wait(crtc);

	/*
	 * Gen2 reports pipe underruns whenever all planes are disabled.
	 * So disable underrun reporting before all the planes get disabled.
	 */
	if (DISPLAY_VER(display) == 2 && !crtc_state->active_planes)
		intel_set_cpu_fifo_underrun_reporting(display, crtc->pipe, false);

	intel_plane_disable_arm(NULL, plane, crtc_state);
	intel_plane_initial_vblank_wait(crtc);
}

unsigned int
intel_plane_fence_y_offset(const struct intel_plane_state *plane_state)
{
	int x = 0, y = 0;

	intel_plane_adjust_aligned_offset(&x, &y, plane_state, 0,
					  plane_state->view.color_plane[0].offset, 0);

	return y;
}

static void icl_set_pipe_chicken(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	tmp = intel_de_read(display, PIPE_CHICKEN(pipe));

	/*
	 * Display WA #1153: icl
	 * enable hardware to bypass the alpha math
	 * and rounding for per-pixel values 00 and 0xff
	 */
	tmp |= PER_PIXEL_ALPHA_BYPASS_EN;
	/*
	 * Display WA # 1605353570: icl
	 * Set the pixel rounding bit to 1 for allowing
	 * passthrough of Frame buffer pixels unmodified
	 * across pipe
	 */
	tmp |= PIXEL_ROUNDING_TRUNC_FB_PASSTHRU;

	/*
	 * Underrun recovery must always be disabled on display 13+.
	 * DG2 chicken bit meaning is inverted compared to other platforms.
	 */
	if (display->platform.dg2)
		tmp &= ~UNDERRUN_RECOVERY_ENABLE_DG2;
	else if ((DISPLAY_VER(display) >= 13) && (DISPLAY_VER(display) < 30))
		tmp |= UNDERRUN_RECOVERY_DISABLE_ADLP;

	/* Wa_14010547955:dg2 */
	if (display->platform.dg2)
		tmp |= DG2_RENDER_CCSTAG_4_3_EN;

	intel_de_write(display, PIPE_CHICKEN(pipe), tmp);
}

bool intel_has_pending_fb_unpin(struct intel_display *display)
{
	struct drm_crtc *crtc;
	bool cleanup_done;

	drm_for_each_crtc(crtc, display->drm) {
		struct drm_crtc_commit *commit;
		spin_lock(&crtc->commit_lock);
		commit = list_first_entry_or_null(&crtc->commit_list,
						  struct drm_crtc_commit, commit_entry);
		cleanup_done = commit ?
			try_wait_for_completion(&commit->cleanup_done) : true;
		spin_unlock(&crtc->commit_lock);

		if (cleanup_done)
			continue;

		intel_crtc_wait_for_next_vblank(to_intel_crtc(crtc));

		return true;
	}

	return false;
}

/*
 * Finds the encoder associated with the given CRTC. This can only be
 * used when we know that the CRTC isn't feeding multiple encoders!
 */
struct intel_encoder *
intel_get_crtc_new_encoder(const struct intel_atomic_state *state,
			   const struct intel_crtc_state *crtc_state)
{
	const struct drm_connector_state *connector_state;
	const struct drm_connector *connector;
	struct intel_encoder *encoder = NULL;
	struct intel_crtc *primary_crtc;
	int num_encoders = 0;
	int i;

	primary_crtc = intel_primary_crtc(crtc_state);

	for_each_new_connector_in_state(&state->base, connector, connector_state, i) {
		if (connector_state->crtc != &primary_crtc->base)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);
		num_encoders++;
	}

	drm_WARN(state->base.dev, num_encoders != 1,
		 "%d encoders for pipe %c\n",
		 num_encoders, pipe_name(primary_crtc->pipe));

	return encoder;
}

static void intel_crtc_dpms_overlay_disable(struct intel_crtc *crtc)
{
	if (crtc->overlay)
		(void) intel_overlay_switch_off(crtc->overlay);

	/* Let userspace switch the overlay on again. In most cases userspace
	 * has to recompute where to put it anyway.
	 */
}

static bool needs_nv12_wa(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (!crtc_state->nv12_planes)
		return false;

	/* WA Display #0827: Gen9:all */
	if (DISPLAY_VER(display) == 9)
		return true;

	return false;
}

static bool needs_scalerclk_wa(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/* Wa_2006604312:icl,ehl */
	if (crtc_state->scaler_state.scaler_users > 0 && DISPLAY_VER(display) == 11)
		return true;

	return false;
}

static bool needs_cursorclk_wa(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/* Wa_1604331009:icl,jsl,ehl */
	if (is_hdr_mode(crtc_state) &&
	    crtc_state->active_planes & BIT(PLANE_CURSOR) &&
	    DISPLAY_VER(display) == 11)
		return true;

	return false;
}

static void intel_async_flip_vtd_wa(struct intel_display *display,
				    enum pipe pipe, bool enable)
{
	if (DISPLAY_VER(display) == 9) {
		/*
		 * "Plane N stretch max must be programmed to 11b (x1)
		 *  when Async flips are enabled on that plane."
		 */
		intel_de_rmw(display, CHICKEN_PIPESL_1(pipe),
			     SKL_PLANE1_STRETCH_MAX_MASK,
			     enable ? SKL_PLANE1_STRETCH_MAX_X1 : SKL_PLANE1_STRETCH_MAX_X8);
	} else {
		/* Also needed on HSW/BDW albeit undocumented */
		intel_de_rmw(display, CHICKEN_PIPESL_1(pipe),
			     HSW_PRI_STRETCH_MAX_MASK,
			     enable ? HSW_PRI_STRETCH_MAX_X1 : HSW_PRI_STRETCH_MAX_X8);
	}
}

static bool needs_async_flip_vtd_wa(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	return crtc_state->uapi.async_flip && i915_vtd_active(i915) &&
		(DISPLAY_VER(display) == 9 || display->platform.broadwell ||
		 display->platform.haswell);
}

static void intel_encoders_audio_enable(struct intel_atomic_state *state,
					struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	for_each_new_connector_in_state(&state->base, conn, conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != &crtc->base)
			continue;

		if (encoder->audio_enable)
			encoder->audio_enable(encoder, crtc_state, conn_state);
	}
}

static void intel_encoders_audio_disable(struct intel_atomic_state *state,
					 struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_old_connector_in_state(&state->base, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != &crtc->base)
			continue;

		if (encoder->audio_disable)
			encoder->audio_disable(encoder, old_crtc_state, old_conn_state);
	}
}

#define is_enabling(feature, old_crtc_state, new_crtc_state) \
	((!(old_crtc_state)->feature || intel_crtc_needs_modeset(new_crtc_state)) && \
	 (new_crtc_state)->feature)
#define is_disabling(feature, old_crtc_state, new_crtc_state) \
	((old_crtc_state)->feature && \
	 (!(new_crtc_state)->feature || intel_crtc_needs_modeset(new_crtc_state)))

static bool planes_enabling(const struct intel_crtc_state *old_crtc_state,
			    const struct intel_crtc_state *new_crtc_state)
{
	if (!new_crtc_state->hw.active)
		return false;

	return is_enabling(active_planes, old_crtc_state, new_crtc_state);
}

static bool planes_disabling(const struct intel_crtc_state *old_crtc_state,
			     const struct intel_crtc_state *new_crtc_state)
{
	if (!old_crtc_state->hw.active)
		return false;

	return is_disabling(active_planes, old_crtc_state, new_crtc_state);
}

static bool vrr_params_changed(const struct intel_crtc_state *old_crtc_state,
			       const struct intel_crtc_state *new_crtc_state)
{
	return old_crtc_state->vrr.flipline != new_crtc_state->vrr.flipline ||
		old_crtc_state->vrr.vmin != new_crtc_state->vrr.vmin ||
		old_crtc_state->vrr.vmax != new_crtc_state->vrr.vmax ||
		old_crtc_state->vrr.guardband != new_crtc_state->vrr.guardband ||
		old_crtc_state->vrr.pipeline_full != new_crtc_state->vrr.pipeline_full ||
		old_crtc_state->vrr.vsync_start != new_crtc_state->vrr.vsync_start ||
		old_crtc_state->vrr.vsync_end != new_crtc_state->vrr.vsync_end;
}

static bool cmrr_params_changed(const struct intel_crtc_state *old_crtc_state,
				const struct intel_crtc_state *new_crtc_state)
{
	return old_crtc_state->cmrr.cmrr_m != new_crtc_state->cmrr.cmrr_m ||
		old_crtc_state->cmrr.cmrr_n != new_crtc_state->cmrr.cmrr_n;
}

static bool intel_crtc_vrr_enabling(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!new_crtc_state->hw.active)
		return false;

	return is_enabling(vrr.enable, old_crtc_state, new_crtc_state) ||
		(new_crtc_state->vrr.enable &&
		 (new_crtc_state->update_m_n || new_crtc_state->update_lrr ||
		  vrr_params_changed(old_crtc_state, new_crtc_state)));
}

bool intel_crtc_vrr_disabling(struct intel_atomic_state *state,
			      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!old_crtc_state->hw.active)
		return false;

	return is_disabling(vrr.enable, old_crtc_state, new_crtc_state) ||
		(old_crtc_state->vrr.enable &&
		 (new_crtc_state->update_m_n || new_crtc_state->update_lrr ||
		  vrr_params_changed(old_crtc_state, new_crtc_state)));
}

static bool audio_enabling(const struct intel_crtc_state *old_crtc_state,
			   const struct intel_crtc_state *new_crtc_state)
{
	if (!new_crtc_state->hw.active)
		return false;

	return is_enabling(has_audio, old_crtc_state, new_crtc_state) ||
		(new_crtc_state->has_audio &&
		 memcmp(old_crtc_state->eld, new_crtc_state->eld, MAX_ELD_BYTES) != 0);
}

static bool audio_disabling(const struct intel_crtc_state *old_crtc_state,
			    const struct intel_crtc_state *new_crtc_state)
{
	if (!old_crtc_state->hw.active)
		return false;

	return is_disabling(has_audio, old_crtc_state, new_crtc_state) ||
		(old_crtc_state->has_audio &&
		 memcmp(old_crtc_state->eld, new_crtc_state->eld, MAX_ELD_BYTES) != 0);
}

#undef is_disabling
#undef is_enabling

static void intel_post_plane_update(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	intel_frontbuffer_flip(display, new_crtc_state->fb_bits);

	if (new_crtc_state->update_wm_post && new_crtc_state->hw.active)
		intel_update_watermarks(display);

	intel_fbc_post_update(state, crtc);

	if (needs_async_flip_vtd_wa(old_crtc_state) &&
	    !needs_async_flip_vtd_wa(new_crtc_state))
		intel_async_flip_vtd_wa(display, pipe, false);

	if (needs_nv12_wa(old_crtc_state) &&
	    !needs_nv12_wa(new_crtc_state))
		skl_wa_827(display, pipe, false);

	if (needs_scalerclk_wa(old_crtc_state) &&
	    !needs_scalerclk_wa(new_crtc_state))
		icl_wa_scalerclkgating(display, pipe, false);

	if (needs_cursorclk_wa(old_crtc_state) &&
	    !needs_cursorclk_wa(new_crtc_state))
		icl_wa_cursorclkgating(display, pipe, false);

	if (intel_crtc_needs_color_update(new_crtc_state))
		intel_color_post_update(new_crtc_state);

	if (audio_enabling(old_crtc_state, new_crtc_state))
		intel_encoders_audio_enable(state, crtc);

	intel_alpm_post_plane_update(state, crtc);

	intel_psr_post_plane_update(state, crtc);
}

static void intel_post_plane_update_after_readout(struct intel_atomic_state *state,
						  struct intel_crtc *crtc)
{
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/* Must be done after gamma readout due to HSW split gamma vs. IPS w/a */
	hsw_ips_post_update(state, crtc);

	/*
	 * Activate DRRS after state readout to avoid
	 * dp_m_n vs. dp_m2_n2 confusion on BDW+.
	 */
	intel_drrs_activate(new_crtc_state);
}

static void intel_crtc_enable_flip_done(struct intel_atomic_state *state,
					struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	u8 update_planes = crtc_state->update_planes;
	const struct intel_plane_state __maybe_unused *plane_state;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane->pipe == crtc->pipe &&
		    update_planes & BIT(plane->id))
			plane->enable_flip_done(plane);
	}
}

static void intel_crtc_disable_flip_done(struct intel_atomic_state *state,
					 struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	u8 update_planes = crtc_state->update_planes;
	const struct intel_plane_state __maybe_unused *plane_state;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane->pipe == crtc->pipe &&
		    update_planes & BIT(plane->id))
			plane->disable_flip_done(plane);
	}
}

static void intel_crtc_async_flip_disable_wa(struct intel_atomic_state *state,
					     struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	u8 disable_async_flip_planes = old_crtc_state->async_flip_planes &
				       ~new_crtc_state->async_flip_planes;
	const struct intel_plane_state *old_plane_state;
	struct intel_plane *plane;
	bool need_vbl_wait = false;
	int i;

	for_each_old_intel_plane_in_state(state, plane, old_plane_state, i) {
		if (plane->need_async_flip_toggle_wa &&
		    plane->pipe == crtc->pipe &&
		    disable_async_flip_planes & BIT(plane->id)) {
			/*
			 * Apart from the async flip bit we want to
			 * preserve the old state for the plane.
			 */
			intel_plane_async_flip(NULL, plane,
					       old_crtc_state, old_plane_state, false);
			need_vbl_wait = true;
		}
	}

	if (need_vbl_wait)
		intel_crtc_wait_for_next_vblank(crtc);
}

static void intel_pre_plane_update(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	intel_alpm_pre_plane_update(state, crtc);
	intel_psr_pre_plane_update(state, crtc);

	if (intel_crtc_vrr_disabling(state, crtc)) {
		intel_vrr_disable(old_crtc_state);
		intel_crtc_update_active_timings(old_crtc_state, false);
	}

	if (audio_disabling(old_crtc_state, new_crtc_state))
		intel_encoders_audio_disable(state, crtc);

	intel_drrs_deactivate(old_crtc_state);

	if (hsw_ips_pre_update(state, crtc))
		intel_crtc_wait_for_next_vblank(crtc);

	if (intel_fbc_pre_update(state, crtc))
		intel_crtc_wait_for_next_vblank(crtc);

	if (!needs_async_flip_vtd_wa(old_crtc_state) &&
	    needs_async_flip_vtd_wa(new_crtc_state))
		intel_async_flip_vtd_wa(display, pipe, true);

	/* Display WA 827 */
	if (!needs_nv12_wa(old_crtc_state) &&
	    needs_nv12_wa(new_crtc_state))
		skl_wa_827(display, pipe, true);

	/* Wa_2006604312:icl,ehl */
	if (!needs_scalerclk_wa(old_crtc_state) &&
	    needs_scalerclk_wa(new_crtc_state))
		icl_wa_scalerclkgating(display, pipe, true);

	/* Wa_1604331009:icl,jsl,ehl */
	if (!needs_cursorclk_wa(old_crtc_state) &&
	    needs_cursorclk_wa(new_crtc_state))
		icl_wa_cursorclkgating(display, pipe, true);

	/*
	 * Vblank time updates from the shadow to live plane control register
	 * are blocked if the memory self-refresh mode is active at that
	 * moment. So to make sure the plane gets truly disabled, disable
	 * first the self-refresh mode. The self-refresh enable bit in turn
	 * will be checked/applied by the HW only at the next frame start
	 * event which is after the vblank start event, so we need to have a
	 * wait-for-vblank between disabling the plane and the pipe.
	 */
	if (HAS_GMCH(display) && old_crtc_state->hw.active &&
	    new_crtc_state->disable_cxsr && intel_set_memory_cxsr(display, false))
		intel_crtc_wait_for_next_vblank(crtc);

	/*
	 * IVB workaround: must disable low power watermarks for at least
	 * one frame before enabling scaling.  LP watermarks can be re-enabled
	 * when scaling is disabled.
	 *
	 * WaCxSRDisabledForSpriteScaling:ivb
	 */
	if (!HAS_GMCH(display) && old_crtc_state->hw.active &&
	    new_crtc_state->disable_cxsr && ilk_disable_cxsr(display))
		intel_crtc_wait_for_next_vblank(crtc);

	/*
	 * If we're doing a modeset we don't need to do any
	 * pre-vblank watermark programming here.
	 */
	if (!intel_crtc_needs_modeset(new_crtc_state)) {
		/*
		 * For platforms that support atomic watermarks, program the
		 * 'intermediate' watermarks immediately.  On pre-gen9 platforms, these
		 * will be the intermediate values that are safe for both pre- and
		 * post- vblank; when vblank happens, the 'active' values will be set
		 * to the final 'target' values and we'll do this again to get the
		 * optimal watermarks.  For gen9+ platforms, the values we program here
		 * will be the final target values which will get automatically latched
		 * at vblank time; no further programming will be necessary.
		 *
		 * If a platform hasn't been transitioned to atomic watermarks yet,
		 * we'll continue to update watermarks the old way, if flags tell
		 * us to.
		 */
		if (!intel_initial_watermarks(state, crtc))
			if (new_crtc_state->update_wm_pre)
				intel_update_watermarks(display);
	}

	/*
	 * Gen2 reports pipe underruns whenever all planes are disabled.
	 * So disable underrun reporting before all the planes get disabled.
	 *
	 * We do this after .initial_watermarks() so that we have a
	 * chance of catching underruns with the intermediate watermarks
	 * vs. the old plane configuration.
	 */
	if (DISPLAY_VER(display) == 2 && planes_disabling(old_crtc_state, new_crtc_state))
		intel_set_cpu_fifo_underrun_reporting(display, pipe, false);

	/*
	 * WA for platforms where async address update enable bit
	 * is double buffered and only latched at start of vblank.
	 */
	if (old_crtc_state->async_flip_planes & ~new_crtc_state->async_flip_planes)
		intel_crtc_async_flip_disable_wa(state, crtc);
}

static void intel_crtc_disable_planes(struct intel_atomic_state *state,
				      struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	unsigned int update_mask = new_crtc_state->update_planes;
	const struct intel_plane_state *old_plane_state;
	struct intel_plane *plane;
	unsigned fb_bits = 0;
	int i;

	intel_crtc_dpms_overlay_disable(crtc);

	for_each_old_intel_plane_in_state(state, plane, old_plane_state, i) {
		if (crtc->pipe != plane->pipe ||
		    !(update_mask & BIT(plane->id)))
			continue;

		intel_plane_disable_arm(NULL, plane, new_crtc_state);

		if (old_plane_state->uapi.visible)
			fb_bits |= plane->frontbuffer_bit;
	}

	intel_frontbuffer_flip(display, fb_bits);
}

static void intel_encoders_update_prepare(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_crtc *crtc;
	int i;

	/*
	 * Make sure the DPLL state is up-to-date for fastset TypeC ports after non-blocking commits.
	 * TODO: Update the DPLL state for all cases in the encoder->update_prepare() hook.
	 */
	if (display->dpll.mgr) {
		for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
			if (intel_crtc_needs_modeset(new_crtc_state))
				continue;

			new_crtc_state->intel_dpll = old_crtc_state->intel_dpll;
			new_crtc_state->dpll_hw_state = old_crtc_state->dpll_hw_state;
		}
	}
}

static void intel_encoders_pre_pll_enable(struct intel_atomic_state *state,
					  struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	for_each_new_connector_in_state(&state->base, conn, conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != &crtc->base)
			continue;

		if (encoder->pre_pll_enable)
			encoder->pre_pll_enable(state, encoder,
						crtc_state, conn_state);
	}
}

static void intel_encoders_pre_enable(struct intel_atomic_state *state,
				      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	for_each_new_connector_in_state(&state->base, conn, conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != &crtc->base)
			continue;

		if (encoder->pre_enable)
			encoder->pre_enable(state, encoder,
					    crtc_state, conn_state);
	}
}

static void intel_encoders_enable(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	for_each_new_connector_in_state(&state->base, conn, conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != &crtc->base)
			continue;

		if (encoder->enable)
			encoder->enable(state, encoder,
					crtc_state, conn_state);
		intel_opregion_notify_encoder(encoder, true);
	}
}

static void intel_encoders_disable(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_old_connector_in_state(&state->base, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != &crtc->base)
			continue;

		intel_opregion_notify_encoder(encoder, false);
		if (encoder->disable)
			encoder->disable(state, encoder,
					 old_crtc_state, old_conn_state);
	}
}

static void intel_encoders_post_disable(struct intel_atomic_state *state,
					struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_old_connector_in_state(&state->base, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != &crtc->base)
			continue;

		if (encoder->post_disable)
			encoder->post_disable(state, encoder,
					      old_crtc_state, old_conn_state);
	}
}

static void intel_encoders_post_pll_disable(struct intel_atomic_state *state,
					    struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct drm_connector_state *old_conn_state;
	struct drm_connector *conn;
	int i;

	for_each_old_connector_in_state(&state->base, conn, old_conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(old_conn_state->best_encoder);

		if (old_conn_state->crtc != &crtc->base)
			continue;

		if (encoder->post_pll_disable)
			encoder->post_pll_disable(state, encoder,
						  old_crtc_state, old_conn_state);
	}
}

static void intel_encoders_update_pipe(struct intel_atomic_state *state,
				       struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	for_each_new_connector_in_state(&state->base, conn, conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);

		if (conn_state->crtc != &crtc->base)
			continue;

		if (encoder->update_pipe)
			encoder->update_pipe(state, encoder,
					     crtc_state, conn_state);
	}
}

static void ilk_configure_cpu_transcoder(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (crtc_state->has_pch_encoder) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->fdi_m_n);
	} else if (intel_crtc_has_dp_encoder(crtc_state)) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->dp_m_n);
		intel_cpu_transcoder_set_m2_n2(crtc, cpu_transcoder,
					       &crtc_state->dp_m2_n2);
	}

	intel_set_transcoder_timings(crtc_state);

	ilk_set_pipeconf(crtc_state);
}

static void ilk_crtc_enable(struct intel_atomic_state *state,
			    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	if (drm_WARN_ON(display->drm, crtc->active))
		return;

	/*
	 * Sometimes spurious CPU pipe underruns happen during FDI
	 * training, at least with VGA+HDMI cloning. Suppress them.
	 *
	 * On ILK we get an occasional spurious CPU pipe underruns
	 * between eDP port A enable and vdd enable. Also PCH port
	 * enable seems to result in the occasional CPU pipe underrun.
	 *
	 * Spurious PCH underruns also occur during PCH enabling.
	 */
	intel_set_cpu_fifo_underrun_reporting(display, pipe, false);
	intel_set_pch_fifo_underrun_reporting(display, pipe, false);

	ilk_configure_cpu_transcoder(new_crtc_state);

	intel_set_pipe_src_size(new_crtc_state);

	crtc->active = true;

	intel_encoders_pre_enable(state, crtc);

	if (new_crtc_state->has_pch_encoder) {
		ilk_pch_pre_enable(state, crtc);
	} else {
		assert_fdi_tx_disabled(display, pipe);
		assert_fdi_rx_disabled(display, pipe);
	}

	ilk_pfit_enable(new_crtc_state);

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_color_modeset(new_crtc_state);

	intel_initial_watermarks(state, crtc);
	intel_enable_transcoder(new_crtc_state);

	if (new_crtc_state->has_pch_encoder)
		ilk_pch_enable(state, crtc);

	intel_crtc_vblank_on(new_crtc_state);

	intel_encoders_enable(state, crtc);

	if (HAS_PCH_CPT(display))
		intel_wait_for_pipe_scanline_moving(crtc);

	/*
	 * Must wait for vblank to avoid spurious PCH FIFO underruns.
	 * And a second vblank wait is needed at least on ILK with
	 * some interlaced HDMI modes. Let's do the double wait always
	 * in case there are more corner cases we don't know about.
	 */
	if (new_crtc_state->has_pch_encoder) {
		intel_crtc_wait_for_next_vblank(crtc);
		intel_crtc_wait_for_next_vblank(crtc);
	}
	intel_set_cpu_fifo_underrun_reporting(display, pipe, true);
	intel_set_pch_fifo_underrun_reporting(display, pipe, true);
}

/* Display WA #1180: WaDisableScalarClockGating: glk */
static bool glk_need_scaler_clock_gating_wa(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return DISPLAY_VER(display) == 10 && crtc_state->pch_pfit.enabled;
}

static void glk_pipe_scaler_clock_gating_wa(struct intel_crtc *crtc, bool enable)
{
	struct intel_display *display = to_intel_display(crtc);
	u32 mask = DPF_GATING_DIS | DPF_RAM_GATING_DIS | DPFR_GATING_DIS;

	intel_de_rmw(display, CLKGATE_DIS_PSL(crtc->pipe),
		     mask, enable ? mask : 0);
}

static void hsw_set_linetime_wm(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	intel_de_write(display, WM_LINETIME(crtc->pipe),
		       HSW_LINETIME(crtc_state->linetime) |
		       HSW_IPS_LINETIME(crtc_state->ips_linetime));
}

static void hsw_set_frame_start_delay(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	intel_de_rmw(display, CHICKEN_TRANS(display, crtc_state->cpu_transcoder),
		     HSW_FRAME_START_DELAY_MASK,
		     HSW_FRAME_START_DELAY(crtc_state->framestart_delay - 1));
}

static void hsw_configure_cpu_transcoder(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (crtc_state->has_pch_encoder) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->fdi_m_n);
	} else if (intel_crtc_has_dp_encoder(crtc_state)) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->dp_m_n);
		intel_cpu_transcoder_set_m2_n2(crtc, cpu_transcoder,
					       &crtc_state->dp_m2_n2);
	}

	intel_set_transcoder_timings(crtc_state);
	if (HAS_VRR(display))
		intel_vrr_set_transcoder_timings(crtc_state);

	if (cpu_transcoder != TRANSCODER_EDP)
		intel_de_write(display, TRANS_MULT(display, cpu_transcoder),
			       crtc_state->pixel_multiplier - 1);

	hsw_set_frame_start_delay(crtc_state);

	hsw_set_transconf(crtc_state);
}

static void hsw_crtc_enable(struct intel_atomic_state *state,
			    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum transcoder cpu_transcoder = new_crtc_state->cpu_transcoder;
	struct intel_crtc *pipe_crtc;
	int i;

	if (drm_WARN_ON(display->drm, crtc->active))
		return;
	for_each_pipe_crtc_modeset_enable(display, pipe_crtc, new_crtc_state, i) {
		const struct intel_crtc_state *new_pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		intel_dmc_enable_pipe(new_pipe_crtc_state);
	}

	intel_encoders_pre_pll_enable(state, crtc);

	if (new_crtc_state->intel_dpll)
		intel_dpll_enable(new_crtc_state);

	intel_encoders_pre_enable(state, crtc);

	for_each_pipe_crtc_modeset_enable(display, pipe_crtc, new_crtc_state, i) {
		const struct intel_crtc_state *pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		intel_dsc_enable(pipe_crtc_state);

		if (HAS_UNCOMPRESSED_JOINER(display))
			intel_uncompressed_joiner_enable(pipe_crtc_state);

		intel_set_pipe_src_size(pipe_crtc_state);

		if (DISPLAY_VER(display) >= 9 || display->platform.broadwell)
			bdw_set_pipe_misc(NULL, pipe_crtc_state);
	}

	if (!transcoder_is_dsi(cpu_transcoder))
		hsw_configure_cpu_transcoder(new_crtc_state);

	for_each_pipe_crtc_modeset_enable(display, pipe_crtc, new_crtc_state, i) {
		const struct intel_crtc_state *pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		pipe_crtc->active = true;

		if (glk_need_scaler_clock_gating_wa(pipe_crtc_state))
			glk_pipe_scaler_clock_gating_wa(pipe_crtc, true);

		if (DISPLAY_VER(display) >= 9)
			skl_pfit_enable(pipe_crtc_state);
		else
			ilk_pfit_enable(pipe_crtc_state);

		/*
		 * On ILK+ LUT must be loaded before the pipe is running but with
		 * clocks enabled
		 */
		intel_color_modeset(pipe_crtc_state);

		hsw_set_linetime_wm(pipe_crtc_state);

		if (DISPLAY_VER(display) >= 11)
			icl_set_pipe_chicken(pipe_crtc_state);

		intel_initial_watermarks(state, pipe_crtc);
	}

	intel_encoders_enable(state, crtc);

	for_each_pipe_crtc_modeset_enable(display, pipe_crtc, new_crtc_state, i) {
		const struct intel_crtc_state *pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);
		enum pipe hsw_workaround_pipe;

		if (glk_need_scaler_clock_gating_wa(pipe_crtc_state)) {
			intel_crtc_wait_for_next_vblank(pipe_crtc);
			glk_pipe_scaler_clock_gating_wa(pipe_crtc, false);
		}

		/*
		 * If we change the relative order between pipe/planes
		 * enabling, we need to change the workaround.
		 */
		hsw_workaround_pipe = pipe_crtc_state->hsw_workaround_pipe;
		if (display->platform.haswell && hsw_workaround_pipe != INVALID_PIPE) {
			struct intel_crtc *wa_crtc =
				intel_crtc_for_pipe(display, hsw_workaround_pipe);

			intel_crtc_wait_for_next_vblank(wa_crtc);
			intel_crtc_wait_for_next_vblank(wa_crtc);
		}
	}
}

static void ilk_crtc_disable(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	/*
	 * Sometimes spurious CPU pipe underruns happen when the
	 * pipe is already disabled, but FDI RX/TX is still enabled.
	 * Happens at least with VGA+HDMI cloning. Suppress them.
	 */
	intel_set_cpu_fifo_underrun_reporting(display, pipe, false);
	intel_set_pch_fifo_underrun_reporting(display, pipe, false);

	intel_encoders_disable(state, crtc);

	intel_crtc_vblank_off(old_crtc_state);

	intel_disable_transcoder(old_crtc_state);

	ilk_pfit_disable(old_crtc_state);

	if (old_crtc_state->has_pch_encoder)
		ilk_pch_disable(state, crtc);

	intel_encoders_post_disable(state, crtc);

	if (old_crtc_state->has_pch_encoder)
		ilk_pch_post_disable(state, crtc);

	intel_set_cpu_fifo_underrun_reporting(display, pipe, true);
	intel_set_pch_fifo_underrun_reporting(display, pipe, true);
}

static void hsw_crtc_disable(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc *pipe_crtc;
	int i;

	/*
	 * FIXME collapse everything to one hook.
	 * Need care with mst->ddi interactions.
	 */
	intel_encoders_disable(state, crtc);
	intel_encoders_post_disable(state, crtc);

	intel_dpll_disable(old_crtc_state);

	intel_encoders_post_pll_disable(state, crtc);

	for_each_pipe_crtc_modeset_disable(display, pipe_crtc, old_crtc_state, i) {
		const struct intel_crtc_state *old_pipe_crtc_state =
			intel_atomic_get_old_crtc_state(state, pipe_crtc);

		intel_dmc_disable_pipe(old_pipe_crtc_state);
	}
}

/* Prefer intel_encoder_is_combo() */
bool intel_phy_is_combo(struct intel_display *display, enum phy phy)
{
	if (phy == PHY_NONE)
		return false;
	else if (display->platform.alderlake_s)
		return phy <= PHY_E;
	else if (display->platform.dg1 || display->platform.rocketlake)
		return phy <= PHY_D;
	else if (display->platform.jasperlake || display->platform.elkhartlake)
		return phy <= PHY_C;
	else if (display->platform.alderlake_p || IS_DISPLAY_VER(display, 11, 12))
		return phy <= PHY_B;
	else
		/*
		 * DG2 outputs labelled as "combo PHY" in the bspec use
		 * SNPS PHYs with completely different programming,
		 * hence we always return false here.
		 */
		return false;
}

/* Prefer intel_encoder_is_tc() */
bool intel_phy_is_tc(struct intel_display *display, enum phy phy)
{
	/*
	 * Discrete GPU phy's are not attached to FIA's to support TC
	 * subsystem Legacy or non-legacy, and only support native DP/HDMI
	 */
	if (display->platform.dgfx)
		return false;

	if (DISPLAY_VER(display) >= 13)
		return phy >= PHY_F && phy <= PHY_I;
	else if (display->platform.tigerlake)
		return phy >= PHY_D && phy <= PHY_I;
	else if (display->platform.icelake)
		return phy >= PHY_C && phy <= PHY_F;

	return false;
}

/* Prefer intel_encoder_is_snps() */
bool intel_phy_is_snps(struct intel_display *display, enum phy phy)
{
	/*
	 * For DG2, and for DG2 only, all four "combo" ports and the TC1 port
	 * (PHY E) use Synopsis PHYs. See intel_phy_is_tc().
	 */
	return display->platform.dg2 && phy > PHY_NONE && phy <= PHY_E;
}

/* Prefer intel_encoder_to_phy() */
enum phy intel_port_to_phy(struct intel_display *display, enum port port)
{
	if (DISPLAY_VER(display) >= 13 && port >= PORT_D_XELPD)
		return PHY_D + port - PORT_D_XELPD;
	else if (DISPLAY_VER(display) >= 13 && port >= PORT_TC1)
		return PHY_F + port - PORT_TC1;
	else if (display->platform.alderlake_s && port >= PORT_TC1)
		return PHY_B + port - PORT_TC1;
	else if ((display->platform.dg1 || display->platform.rocketlake) && port >= PORT_TC1)
		return PHY_C + port - PORT_TC1;
	else if ((display->platform.jasperlake || display->platform.elkhartlake) &&
		 port == PORT_D)
		return PHY_A;

	return PHY_A + port - PORT_A;
}

/* Prefer intel_encoder_to_tc() */
enum tc_port intel_port_to_tc(struct intel_display *display, enum port port)
{
	if (!intel_phy_is_tc(display, intel_port_to_phy(display, port)))
		return TC_PORT_NONE;

	if (DISPLAY_VER(display) >= 12)
		return TC_PORT_1 + port - PORT_TC1;
	else
		return TC_PORT_1 + port - PORT_C;
}

enum phy intel_encoder_to_phy(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	return intel_port_to_phy(display, encoder->port);
}

bool intel_encoder_is_combo(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	return intel_phy_is_combo(display, intel_encoder_to_phy(encoder));
}

bool intel_encoder_is_snps(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	return intel_phy_is_snps(display, intel_encoder_to_phy(encoder));
}

bool intel_encoder_is_tc(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	return intel_phy_is_tc(display, intel_encoder_to_phy(encoder));
}

enum tc_port intel_encoder_to_tc(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	return intel_port_to_tc(display, encoder->port);
}

enum intel_display_power_domain
intel_aux_power_domain(struct intel_digital_port *dig_port)
{
	struct intel_display *display = to_intel_display(dig_port);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		return intel_display_power_tbt_aux_domain(display, dig_port->aux_ch);

	return intel_display_power_legacy_aux_domain(display, dig_port->aux_ch);
}

static void get_crtc_power_domains(struct intel_crtc_state *crtc_state,
				   struct intel_power_domain_mask *mask)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	struct drm_encoder *encoder;
	enum pipe pipe = crtc->pipe;

	bitmap_zero(mask->bits, POWER_DOMAIN_NUM);

	if (!crtc_state->hw.active)
		return;

	set_bit(POWER_DOMAIN_PIPE(pipe), mask->bits);
	set_bit(POWER_DOMAIN_TRANSCODER(cpu_transcoder), mask->bits);
	if (crtc_state->pch_pfit.enabled ||
	    crtc_state->pch_pfit.force_thru)
		set_bit(POWER_DOMAIN_PIPE_PANEL_FITTER(pipe), mask->bits);

	drm_for_each_encoder_mask(encoder, display->drm,
				  crtc_state->uapi.encoder_mask) {
		struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

		set_bit(intel_encoder->power_domain, mask->bits);
	}

	if (HAS_DDI(display) && crtc_state->has_audio)
		set_bit(POWER_DOMAIN_AUDIO_MMIO, mask->bits);

	if (crtc_state->intel_dpll)
		set_bit(POWER_DOMAIN_DISPLAY_CORE, mask->bits);

	if (crtc_state->dsc.compression_enable)
		set_bit(intel_dsc_power_domain(crtc, cpu_transcoder), mask->bits);
}

void intel_modeset_get_crtc_power_domains(struct intel_crtc_state *crtc_state,
					  struct intel_power_domain_mask *old_domains)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum intel_display_power_domain domain;
	struct intel_power_domain_mask domains, new_domains;

	get_crtc_power_domains(crtc_state, &domains);

	bitmap_andnot(new_domains.bits,
		      domains.bits,
		      crtc->enabled_power_domains.mask.bits,
		      POWER_DOMAIN_NUM);
	bitmap_andnot(old_domains->bits,
		      crtc->enabled_power_domains.mask.bits,
		      domains.bits,
		      POWER_DOMAIN_NUM);

	for_each_power_domain(domain, &new_domains)
		intel_display_power_get_in_set(display,
					       &crtc->enabled_power_domains,
					       domain);
}

void intel_modeset_put_crtc_power_domains(struct intel_crtc *crtc,
					  struct intel_power_domain_mask *domains)
{
	struct intel_display *display = to_intel_display(crtc);

	intel_display_power_put_mask_in_set(display,
					    &crtc->enabled_power_domains,
					    domains);
}

static void i9xx_configure_cpu_transcoder(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->dp_m_n);
		intel_cpu_transcoder_set_m2_n2(crtc, cpu_transcoder,
					       &crtc_state->dp_m2_n2);
	}

	intel_set_transcoder_timings(crtc_state);

	i9xx_set_pipeconf(crtc_state);
}

static void valleyview_crtc_enable(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	if (drm_WARN_ON(display->drm, crtc->active))
		return;

	i9xx_configure_cpu_transcoder(new_crtc_state);

	intel_set_pipe_src_size(new_crtc_state);

	intel_de_write(display, VLV_PIPE_MSA_MISC(display, pipe), 0);

	if (display->platform.cherryview && pipe == PIPE_B) {
		intel_de_write(display, CHV_BLEND(display, pipe),
			       CHV_BLEND_LEGACY);
		intel_de_write(display, CHV_CANVAS(display, pipe), 0);
	}

	crtc->active = true;

	intel_set_cpu_fifo_underrun_reporting(display, pipe, true);

	intel_encoders_pre_pll_enable(state, crtc);

	if (display->platform.cherryview)
		chv_enable_pll(new_crtc_state);
	else
		vlv_enable_pll(new_crtc_state);

	intel_encoders_pre_enable(state, crtc);

	i9xx_pfit_enable(new_crtc_state);

	intel_color_modeset(new_crtc_state);

	intel_initial_watermarks(state, crtc);
	intel_enable_transcoder(new_crtc_state);

	intel_crtc_vblank_on(new_crtc_state);

	intel_encoders_enable(state, crtc);
}

static void i9xx_crtc_enable(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	if (drm_WARN_ON(display->drm, crtc->active))
		return;

	i9xx_configure_cpu_transcoder(new_crtc_state);

	intel_set_pipe_src_size(new_crtc_state);

	crtc->active = true;

	if (DISPLAY_VER(display) != 2)
		intel_set_cpu_fifo_underrun_reporting(display, pipe, true);

	intel_encoders_pre_enable(state, crtc);

	i9xx_enable_pll(new_crtc_state);

	i9xx_pfit_enable(new_crtc_state);

	intel_color_modeset(new_crtc_state);

	if (!intel_initial_watermarks(state, crtc))
		intel_update_watermarks(display);
	intel_enable_transcoder(new_crtc_state);

	intel_crtc_vblank_on(new_crtc_state);

	intel_encoders_enable(state, crtc);

	/* prevents spurious underruns */
	if (DISPLAY_VER(display) == 2)
		intel_crtc_wait_for_next_vblank(crtc);
}

static void i9xx_crtc_disable(struct intel_atomic_state *state,
			      struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;

	/*
	 * On gen2 planes are double buffered but the pipe isn't, so we must
	 * wait for planes to fully turn off before disabling the pipe.
	 */
	if (DISPLAY_VER(display) == 2)
		intel_crtc_wait_for_next_vblank(crtc);

	intel_encoders_disable(state, crtc);

	intel_crtc_vblank_off(old_crtc_state);

	intel_disable_transcoder(old_crtc_state);

	i9xx_pfit_disable(old_crtc_state);

	intel_encoders_post_disable(state, crtc);

	if (!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DSI)) {
		if (display->platform.cherryview)
			chv_disable_pll(display, pipe);
		else if (display->platform.valleyview)
			vlv_disable_pll(display, pipe);
		else
			i9xx_disable_pll(old_crtc_state);
	}

	intel_encoders_post_pll_disable(state, crtc);

	if (DISPLAY_VER(display) != 2)
		intel_set_cpu_fifo_underrun_reporting(display, pipe, false);

	if (!display->funcs.wm->initial_watermarks)
		intel_update_watermarks(display);

	/* clock the pipe down to 640x480@60 to potentially save power */
	if (display->platform.i830)
		i830_enable_pipe(display, pipe);
}

void intel_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(intel_encoder);
}

static bool intel_crtc_supports_double_wide(const struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	/* GDG double wide on either pipe, otherwise pipe A only */
	return HAS_DOUBLE_WIDE(display) &&
		(crtc->pipe == PIPE_A || display->platform.i915g);
}

static u32 ilk_pipe_pixel_rate(const struct intel_crtc_state *crtc_state)
{
	u32 pixel_rate = crtc_state->hw.pipe_mode.crtc_clock;
	struct drm_rect src;

	/*
	 * We only use IF-ID interlacing. If we ever use
	 * PF-ID we'll need to adjust the pixel_rate here.
	 */

	if (!crtc_state->pch_pfit.enabled)
		return pixel_rate;

	drm_rect_init(&src, 0, 0,
		      drm_rect_width(&crtc_state->pipe_src) << 16,
		      drm_rect_height(&crtc_state->pipe_src) << 16);

	return intel_adjusted_rate(&src, &crtc_state->pch_pfit.dst,
				   pixel_rate);
}

static void intel_mode_from_crtc_timings(struct drm_display_mode *mode,
					 const struct drm_display_mode *timings)
{
	mode->hdisplay = timings->crtc_hdisplay;
	mode->htotal = timings->crtc_htotal;
	mode->hsync_start = timings->crtc_hsync_start;
	mode->hsync_end = timings->crtc_hsync_end;

	mode->vdisplay = timings->crtc_vdisplay;
	mode->vtotal = timings->crtc_vtotal;
	mode->vsync_start = timings->crtc_vsync_start;
	mode->vsync_end = timings->crtc_vsync_end;

	mode->flags = timings->flags;
	mode->type = DRM_MODE_TYPE_DRIVER;

	mode->clock = timings->crtc_clock;

	drm_mode_set_name(mode);
}

static void intel_crtc_compute_pixel_rate(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (HAS_GMCH(display))
		/* FIXME calculate proper pipe pixel rate for GMCH pfit */
		crtc_state->pixel_rate =
			crtc_state->hw.pipe_mode.crtc_clock;
	else
		crtc_state->pixel_rate =
			ilk_pipe_pixel_rate(crtc_state);
}

static void intel_joiner_adjust_timings(const struct intel_crtc_state *crtc_state,
					struct drm_display_mode *mode)
{
	int num_pipes = intel_crtc_num_joined_pipes(crtc_state);

	if (num_pipes == 1)
		return;

	mode->crtc_clock /= num_pipes;
	mode->crtc_hdisplay /= num_pipes;
	mode->crtc_hblank_start /= num_pipes;
	mode->crtc_hblank_end /= num_pipes;
	mode->crtc_hsync_start /= num_pipes;
	mode->crtc_hsync_end /= num_pipes;
	mode->crtc_htotal /= num_pipes;
}

static void intel_splitter_adjust_timings(const struct intel_crtc_state *crtc_state,
					  struct drm_display_mode *mode)
{
	int overlap = crtc_state->splitter.pixel_overlap;
	int n = crtc_state->splitter.link_count;

	if (!crtc_state->splitter.enable)
		return;

	/*
	 * eDP MSO uses segment timings from EDID for transcoder
	 * timings, but full mode for everything else.
	 *
	 * h_full = (h_segment - pixel_overlap) * link_count
	 */
	mode->crtc_hdisplay = (mode->crtc_hdisplay - overlap) * n;
	mode->crtc_hblank_start = (mode->crtc_hblank_start - overlap) * n;
	mode->crtc_hblank_end = (mode->crtc_hblank_end - overlap) * n;
	mode->crtc_hsync_start = (mode->crtc_hsync_start - overlap) * n;
	mode->crtc_hsync_end = (mode->crtc_hsync_end - overlap) * n;
	mode->crtc_htotal = (mode->crtc_htotal - overlap) * n;
	mode->crtc_clock *= n;
}

static void intel_crtc_readout_derived_state(struct intel_crtc_state *crtc_state)
{
	struct drm_display_mode *mode = &crtc_state->hw.mode;
	struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	/*
	 * Start with the adjusted_mode crtc timings, which
	 * have been filled with the transcoder timings.
	 */
	drm_mode_copy(pipe_mode, adjusted_mode);

	/* Expand MSO per-segment transcoder timings to full */
	intel_splitter_adjust_timings(crtc_state, pipe_mode);

	/*
	 * We want the full numbers in adjusted_mode normal timings,
	 * adjusted_mode crtc timings are left with the raw transcoder
	 * timings.
	 */
	intel_mode_from_crtc_timings(adjusted_mode, pipe_mode);

	/* Populate the "user" mode with full numbers */
	drm_mode_copy(mode, pipe_mode);
	intel_mode_from_crtc_timings(mode, mode);
	mode->hdisplay = drm_rect_width(&crtc_state->pipe_src) *
		intel_crtc_num_joined_pipes(crtc_state);
	mode->vdisplay = drm_rect_height(&crtc_state->pipe_src);

	/* Derive per-pipe timings in case joiner is used */
	intel_joiner_adjust_timings(crtc_state, pipe_mode);
	intel_mode_from_crtc_timings(pipe_mode, pipe_mode);

	intel_crtc_compute_pixel_rate(crtc_state);
}

void intel_encoder_get_config(struct intel_encoder *encoder,
			      struct intel_crtc_state *crtc_state)
{
	encoder->get_config(encoder, crtc_state);

	intel_crtc_readout_derived_state(crtc_state);
}

static void intel_joiner_compute_pipe_src(struct intel_crtc_state *crtc_state)
{
	int num_pipes = intel_crtc_num_joined_pipes(crtc_state);
	int width, height;

	if (num_pipes == 1)
		return;

	width = drm_rect_width(&crtc_state->pipe_src);
	height = drm_rect_height(&crtc_state->pipe_src);

	drm_rect_init(&crtc_state->pipe_src, 0, 0,
		      width / num_pipes, height);
}

static int intel_crtc_compute_pipe_src(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	intel_joiner_compute_pipe_src(crtc_state);

	/*
	 * Pipe horizontal size must be even in:
	 * - DVO ganged mode
	 * - LVDS dual channel mode
	 * - Double wide pipe
	 */
	if (drm_rect_width(&crtc_state->pipe_src) & 1) {
		if (crtc_state->double_wide) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] Odd pipe source width not supported with double wide pipe\n",
				    crtc->base.base.id, crtc->base.name);
			return -EINVAL;
		}

		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_LVDS) &&
		    intel_is_dual_link_lvds(display)) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] Odd pipe source width not supported with dual link LVDS\n",
				    crtc->base.base.id, crtc->base.name);
			return -EINVAL;
		}
	}

	return 0;
}

static int intel_crtc_compute_pipe_mode(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;
	int clock_limit = display->cdclk.max_dotclk_freq;

	/*
	 * Start with the adjusted_mode crtc timings, which
	 * have been filled with the transcoder timings.
	 */
	drm_mode_copy(pipe_mode, adjusted_mode);

	/* Expand MSO per-segment transcoder timings to full */
	intel_splitter_adjust_timings(crtc_state, pipe_mode);

	/* Derive per-pipe timings in case joiner is used */
	intel_joiner_adjust_timings(crtc_state, pipe_mode);
	intel_mode_from_crtc_timings(pipe_mode, pipe_mode);

	if (DISPLAY_VER(display) < 4) {
		clock_limit = display->cdclk.max_cdclk_freq * 9 / 10;

		/*
		 * Enable double wide mode when the dot clock
		 * is > 90% of the (display) core speed.
		 */
		if (intel_crtc_supports_double_wide(crtc) &&
		    pipe_mode->crtc_clock > clock_limit) {
			clock_limit = display->cdclk.max_dotclk_freq;
			crtc_state->double_wide = true;
		}
	}

	if (pipe_mode->crtc_clock > clock_limit) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] requested pixel clock (%d kHz) too high (max: %d kHz, double wide: %s)\n",
			    crtc->base.base.id, crtc->base.name,
			    pipe_mode->crtc_clock, clock_limit,
			    str_yes_no(crtc_state->double_wide));
		return -EINVAL;
	}

	return 0;
}

static int intel_crtc_vblank_delay(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	int vblank_delay = 0;

	if (!HAS_DSB(display))
		return 0;

	vblank_delay = max(vblank_delay, intel_psr_min_vblank_delay(crtc_state));

	return vblank_delay;
}

static int intel_crtc_compute_vblank_delay(struct intel_atomic_state *state,
					   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int vblank_delay, max_vblank_delay;

	vblank_delay = intel_crtc_vblank_delay(crtc_state);
	max_vblank_delay = adjusted_mode->crtc_vblank_end - adjusted_mode->crtc_vblank_start - 1;

	if (vblank_delay > max_vblank_delay) {
		drm_dbg_kms(display->drm, "[CRTC:%d:%s] vblank delay (%d) exceeds max (%d)\n",
			    crtc->base.base.id, crtc->base.name, vblank_delay, max_vblank_delay);
		return -EINVAL;
	}

	adjusted_mode->crtc_vblank_start += vblank_delay;

	return 0;
}

static int intel_crtc_compute_config(struct intel_atomic_state *state,
				     struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = intel_crtc_compute_vblank_delay(state, crtc);
	if (ret)
		return ret;

	ret = intel_dpll_crtc_compute_clock(state, crtc);
	if (ret)
		return ret;

	ret = intel_crtc_compute_pipe_src(crtc_state);
	if (ret)
		return ret;

	ret = intel_crtc_compute_pipe_mode(crtc_state);
	if (ret)
		return ret;

	intel_crtc_compute_pixel_rate(crtc_state);

	if (crtc_state->has_pch_encoder)
		return ilk_fdi_compute_config(crtc, crtc_state);

	return 0;
}

static void
intel_reduce_m_n_ratio(u32 *num, u32 *den)
{
	while (*num > DATA_LINK_M_N_MASK ||
	       *den > DATA_LINK_M_N_MASK) {
		*num >>= 1;
		*den >>= 1;
	}
}

static void compute_m_n(u32 *ret_m, u32 *ret_n,
			u32 m, u32 n, u32 constant_n)
{
	if (constant_n)
		*ret_n = constant_n;
	else
		*ret_n = min_t(unsigned int, roundup_pow_of_two(n), DATA_LINK_N_MAX);

	*ret_m = div_u64(mul_u32_u32(m, *ret_n), n);
	intel_reduce_m_n_ratio(ret_m, ret_n);
}

void
intel_link_compute_m_n(u16 bits_per_pixel_x16, int nlanes,
		       int pixel_clock, int link_clock,
		       int bw_overhead,
		       struct intel_link_m_n *m_n)
{
	u32 link_symbol_clock = intel_dp_link_symbol_clock(link_clock);
	u32 data_m = intel_dp_effective_data_rate(pixel_clock, bits_per_pixel_x16,
						  bw_overhead);
	u32 data_n = drm_dp_max_dprx_data_rate(link_clock, nlanes);

	/*
	 * Windows/BIOS uses fixed M/N values always. Follow suit.
	 *
	 * Also several DP dongles in particular seem to be fussy
	 * about too large link M/N values. Presumably the 20bit
	 * value used by Windows/BIOS is acceptable to everyone.
	 */
	m_n->tu = 64;
	compute_m_n(&m_n->data_m, &m_n->data_n,
		    data_m, data_n,
		    0x8000000);

	compute_m_n(&m_n->link_m, &m_n->link_n,
		    pixel_clock, link_symbol_clock,
		    0x80000);
}

void intel_panel_sanitize_ssc(struct intel_display *display)
{
	/*
	 * There may be no VBT; and if the BIOS enabled SSC we can
	 * just keep using it to avoid unnecessary flicker.  Whereas if the
	 * BIOS isn't using it, don't assume it will work even if the VBT
	 * indicates as much.
	 */
	if (HAS_PCH_IBX(display) || HAS_PCH_CPT(display)) {
		bool bios_lvds_use_ssc = intel_de_read(display,
						       PCH_DREF_CONTROL) &
			DREF_SSC1_ENABLE;

		if (display->vbt.lvds_use_ssc != bios_lvds_use_ssc) {
			drm_dbg_kms(display->drm,
				    "SSC %s by BIOS, overriding VBT which says %s\n",
				    str_enabled_disabled(bios_lvds_use_ssc),
				    str_enabled_disabled(display->vbt.lvds_use_ssc));
			display->vbt.lvds_use_ssc = bios_lvds_use_ssc;
		}
	}
}

void intel_zero_m_n(struct intel_link_m_n *m_n)
{
	/* corresponds to 0 register value */
	memset(m_n, 0, sizeof(*m_n));
	m_n->tu = 1;
}

void intel_set_m_n(struct intel_display *display,
		   const struct intel_link_m_n *m_n,
		   i915_reg_t data_m_reg, i915_reg_t data_n_reg,
		   i915_reg_t link_m_reg, i915_reg_t link_n_reg)
{
	intel_de_write(display, data_m_reg, TU_SIZE(m_n->tu) | m_n->data_m);
	intel_de_write(display, data_n_reg, m_n->data_n);
	intel_de_write(display, link_m_reg, m_n->link_m);
	/*
	 * On BDW+ writing LINK_N arms the double buffered update
	 * of all the M/N registers, so it must be written last.
	 */
	intel_de_write(display, link_n_reg, m_n->link_n);
}

bool intel_cpu_transcoder_has_m2_n2(struct intel_display *display,
				    enum transcoder transcoder)
{
	if (display->platform.haswell)
		return transcoder == TRANSCODER_EDP;

	return IS_DISPLAY_VER(display, 5, 7) || display->platform.cherryview;
}

void intel_cpu_transcoder_set_m1_n1(struct intel_crtc *crtc,
				    enum transcoder transcoder,
				    const struct intel_link_m_n *m_n)
{
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;

	if (DISPLAY_VER(display) >= 5)
		intel_set_m_n(display, m_n,
			      PIPE_DATA_M1(display, transcoder),
			      PIPE_DATA_N1(display, transcoder),
			      PIPE_LINK_M1(display, transcoder),
			      PIPE_LINK_N1(display, transcoder));
	else
		intel_set_m_n(display, m_n,
			      PIPE_DATA_M_G4X(pipe), PIPE_DATA_N_G4X(pipe),
			      PIPE_LINK_M_G4X(pipe), PIPE_LINK_N_G4X(pipe));
}

void intel_cpu_transcoder_set_m2_n2(struct intel_crtc *crtc,
				    enum transcoder transcoder,
				    const struct intel_link_m_n *m_n)
{
	struct intel_display *display = to_intel_display(crtc);

	if (!intel_cpu_transcoder_has_m2_n2(display, transcoder))
		return;

	intel_set_m_n(display, m_n,
		      PIPE_DATA_M2(display, transcoder),
		      PIPE_DATA_N2(display, transcoder),
		      PIPE_LINK_M2(display, transcoder),
		      PIPE_LINK_N2(display, transcoder));
}

static bool
transcoder_has_vrr(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	return HAS_VRR(display) && !transcoder_is_dsi(cpu_transcoder);
}

static void intel_set_transcoder_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	u32 crtc_vdisplay, crtc_vtotal, crtc_vblank_start, crtc_vblank_end;
	int vsyncshift = 0;

	drm_WARN_ON(display->drm, transcoder_is_dsi(cpu_transcoder));

	/* We need to be careful not to changed the adjusted mode, for otherwise
	 * the hw state checker will get angry at the mismatch. */
	crtc_vdisplay = adjusted_mode->crtc_vdisplay;
	crtc_vtotal = adjusted_mode->crtc_vtotal;
	crtc_vblank_start = adjusted_mode->crtc_vblank_start;
	crtc_vblank_end = adjusted_mode->crtc_vblank_end;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		/* the chip adds 2 halflines automatically */
		crtc_vtotal -= 1;
		crtc_vblank_end -= 1;

		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO))
			vsyncshift = (adjusted_mode->crtc_htotal - 1) / 2;
		else
			vsyncshift = adjusted_mode->crtc_hsync_start -
				adjusted_mode->crtc_htotal / 2;
		if (vsyncshift < 0)
			vsyncshift += adjusted_mode->crtc_htotal;
	}

	/*
	 * VBLANK_START no longer works on ADL+, instead we must use
	 * TRANS_SET_CONTEXT_LATENCY to configure the pipe vblank start.
	 */
	if (DISPLAY_VER(display) >= 13) {
		intel_de_write(display,
			       TRANS_SET_CONTEXT_LATENCY(display, cpu_transcoder),
			       crtc_vblank_start - crtc_vdisplay);

		/*
		 * VBLANK_START not used by hw, just clear it
		 * to make it stand out in register dumps.
		 */
		crtc_vblank_start = 1;
	}

	if (DISPLAY_VER(display) >= 4)
		intel_de_write(display,
			       TRANS_VSYNCSHIFT(display, cpu_transcoder),
			       vsyncshift);

	intel_de_write(display, TRANS_HTOTAL(display, cpu_transcoder),
		       HACTIVE(adjusted_mode->crtc_hdisplay - 1) |
		       HTOTAL(adjusted_mode->crtc_htotal - 1));
	intel_de_write(display, TRANS_HBLANK(display, cpu_transcoder),
		       HBLANK_START(adjusted_mode->crtc_hblank_start - 1) |
		       HBLANK_END(adjusted_mode->crtc_hblank_end - 1));
	intel_de_write(display, TRANS_HSYNC(display, cpu_transcoder),
		       HSYNC_START(adjusted_mode->crtc_hsync_start - 1) |
		       HSYNC_END(adjusted_mode->crtc_hsync_end - 1));

	/*
	 * For platforms that always use VRR Timing Generator, the VTOTAL.Vtotal
	 * bits are not required. Since the support for these bits is going to
	 * be deprecated in upcoming platforms, avoid writing these bits for the
	 * platforms that do not use legacy Timing Generator.
	 */
	if (intel_vrr_always_use_vrr_tg(display))
		crtc_vtotal = 1;

	intel_de_write(display, TRANS_VTOTAL(display, cpu_transcoder),
		       VACTIVE(crtc_vdisplay - 1) |
		       VTOTAL(crtc_vtotal - 1));
	intel_de_write(display, TRANS_VBLANK(display, cpu_transcoder),
		       VBLANK_START(crtc_vblank_start - 1) |
		       VBLANK_END(crtc_vblank_end - 1));
	intel_de_write(display, TRANS_VSYNC(display, cpu_transcoder),
		       VSYNC_START(adjusted_mode->crtc_vsync_start - 1) |
		       VSYNC_END(adjusted_mode->crtc_vsync_end - 1));

	/* Workaround: when the EDP input selection is B, the VTOTAL_B must be
	 * programmed with the VTOTAL_EDP value. Same for VTOTAL_C. This is
	 * documented on the DDI_FUNC_CTL register description, EDP Input Select
	 * bits. */
	if (display->platform.haswell && cpu_transcoder == TRANSCODER_EDP &&
	    (pipe == PIPE_B || pipe == PIPE_C))
		intel_de_write(display, TRANS_VTOTAL(display, pipe),
			       VACTIVE(crtc_vdisplay - 1) |
			       VTOTAL(crtc_vtotal - 1));

	if (DISPLAY_VER(display) >= 30) {
		/*
		 * Address issues for resolutions with high refresh rate that
		 * have small Hblank, specifically where Hblank is smaller than
		 * one MTP. Simulations indicate this will address the
		 * jitter issues that currently causes BS to be immediately
		 * followed by BE which DPRX devices are unable to handle.
		 * https://groups.vesa.org/wg/DP/document/20494
		 */
		intel_de_write(display, DP_MIN_HBLANK_CTL(cpu_transcoder),
			       crtc_state->min_hblank);
	}
}

static void intel_set_transcoder_timings_lrr(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	u32 crtc_vdisplay, crtc_vtotal, crtc_vblank_start, crtc_vblank_end;

	drm_WARN_ON(display->drm, transcoder_is_dsi(cpu_transcoder));

	crtc_vdisplay = adjusted_mode->crtc_vdisplay;
	crtc_vtotal = adjusted_mode->crtc_vtotal;
	crtc_vblank_start = adjusted_mode->crtc_vblank_start;
	crtc_vblank_end = adjusted_mode->crtc_vblank_end;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		/* the chip adds 2 halflines automatically */
		crtc_vtotal -= 1;
		crtc_vblank_end -= 1;
	}

	if (DISPLAY_VER(display) >= 13) {
		intel_de_write(display,
			       TRANS_SET_CONTEXT_LATENCY(display, cpu_transcoder),
			       crtc_vblank_start - crtc_vdisplay);

		/*
		 * VBLANK_START not used by hw, just clear it
		 * to make it stand out in register dumps.
		 */
		crtc_vblank_start = 1;
	}

	/*
	 * The hardware actually ignores TRANS_VBLANK.VBLANK_END in DP mode.
	 * But let's write it anyway to keep the state checker happy.
	 */
	intel_de_write(display, TRANS_VBLANK(display, cpu_transcoder),
		       VBLANK_START(crtc_vblank_start - 1) |
		       VBLANK_END(crtc_vblank_end - 1));
	/*
	 * For platforms that always use VRR Timing Generator, the VTOTAL.Vtotal
	 * bits are not required. Since the support for these bits is going to
	 * be deprecated in upcoming platforms, avoid writing these bits for the
	 * platforms that do not use legacy Timing Generator.
	 */
	if (intel_vrr_always_use_vrr_tg(display))
		crtc_vtotal = 1;

	/*
	 * The double buffer latch point for TRANS_VTOTAL
	 * is the transcoder's undelayed vblank.
	 */
	intel_de_write(display, TRANS_VTOTAL(display, cpu_transcoder),
		       VACTIVE(crtc_vdisplay - 1) |
		       VTOTAL(crtc_vtotal - 1));

	intel_vrr_set_fixed_rr_timings(crtc_state);
	intel_vrr_transcoder_enable(crtc_state);
}

static void intel_set_pipe_src_size(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int width = drm_rect_width(&crtc_state->pipe_src);
	int height = drm_rect_height(&crtc_state->pipe_src);
	enum pipe pipe = crtc->pipe;

	/* pipesrc controls the size that is scaled from, which should
	 * always be the user's requested size.
	 */
	intel_de_write(display, PIPESRC(display, pipe),
		       PIPESRC_WIDTH(width - 1) | PIPESRC_HEIGHT(height - 1));
}

static bool intel_pipe_is_interlaced(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (DISPLAY_VER(display) == 2)
		return false;

	if (DISPLAY_VER(display) >= 9 ||
	    display->platform.broadwell || display->platform.haswell)
		return intel_de_read(display,
				     TRANSCONF(display, cpu_transcoder)) & TRANSCONF_INTERLACE_MASK_HSW;
	else
		return intel_de_read(display,
				     TRANSCONF(display, cpu_transcoder)) & TRANSCONF_INTERLACE_MASK;
}

static void intel_get_transcoder_timings(struct intel_crtc *crtc,
					 struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(crtc);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	u32 tmp;

	tmp = intel_de_read(display, TRANS_HTOTAL(display, cpu_transcoder));
	adjusted_mode->crtc_hdisplay = REG_FIELD_GET(HACTIVE_MASK, tmp) + 1;
	adjusted_mode->crtc_htotal = REG_FIELD_GET(HTOTAL_MASK, tmp) + 1;

	if (!transcoder_is_dsi(cpu_transcoder)) {
		tmp = intel_de_read(display,
				    TRANS_HBLANK(display, cpu_transcoder));
		adjusted_mode->crtc_hblank_start = REG_FIELD_GET(HBLANK_START_MASK, tmp) + 1;
		adjusted_mode->crtc_hblank_end = REG_FIELD_GET(HBLANK_END_MASK, tmp) + 1;
	}

	tmp = intel_de_read(display, TRANS_HSYNC(display, cpu_transcoder));
	adjusted_mode->crtc_hsync_start = REG_FIELD_GET(HSYNC_START_MASK, tmp) + 1;
	adjusted_mode->crtc_hsync_end = REG_FIELD_GET(HSYNC_END_MASK, tmp) + 1;

	tmp = intel_de_read(display, TRANS_VTOTAL(display, cpu_transcoder));
	adjusted_mode->crtc_vdisplay = REG_FIELD_GET(VACTIVE_MASK, tmp) + 1;
	adjusted_mode->crtc_vtotal = REG_FIELD_GET(VTOTAL_MASK, tmp) + 1;

	/* FIXME TGL+ DSI transcoders have this! */
	if (!transcoder_is_dsi(cpu_transcoder)) {
		tmp = intel_de_read(display,
				    TRANS_VBLANK(display, cpu_transcoder));
		adjusted_mode->crtc_vblank_start = REG_FIELD_GET(VBLANK_START_MASK, tmp) + 1;
		adjusted_mode->crtc_vblank_end = REG_FIELD_GET(VBLANK_END_MASK, tmp) + 1;
	}
	tmp = intel_de_read(display, TRANS_VSYNC(display, cpu_transcoder));
	adjusted_mode->crtc_vsync_start = REG_FIELD_GET(VSYNC_START_MASK, tmp) + 1;
	adjusted_mode->crtc_vsync_end = REG_FIELD_GET(VSYNC_END_MASK, tmp) + 1;

	if (intel_pipe_is_interlaced(pipe_config)) {
		adjusted_mode->flags |= DRM_MODE_FLAG_INTERLACE;
		adjusted_mode->crtc_vtotal += 1;
		adjusted_mode->crtc_vblank_end += 1;
	}

	if (DISPLAY_VER(display) >= 13 && !transcoder_is_dsi(cpu_transcoder))
		adjusted_mode->crtc_vblank_start =
			adjusted_mode->crtc_vdisplay +
			intel_de_read(display,
				      TRANS_SET_CONTEXT_LATENCY(display, cpu_transcoder));

	if (DISPLAY_VER(display) >= 30)
		pipe_config->min_hblank = intel_de_read(display,
							DP_MIN_HBLANK_CTL(cpu_transcoder));
}

static void intel_joiner_adjust_pipe_src(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int num_pipes = intel_crtc_num_joined_pipes(crtc_state);
	enum pipe primary_pipe, pipe = crtc->pipe;
	int width;

	if (num_pipes == 1)
		return;

	primary_pipe = joiner_primary_pipe(crtc_state);
	width = drm_rect_width(&crtc_state->pipe_src);

	drm_rect_translate_to(&crtc_state->pipe_src,
			      (pipe - primary_pipe) * width, 0);
}

static void intel_get_pipe_src_size(struct intel_crtc *crtc,
				    struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(crtc);
	u32 tmp;

	tmp = intel_de_read(display, PIPESRC(display, crtc->pipe));

	drm_rect_init(&pipe_config->pipe_src, 0, 0,
		      REG_FIELD_GET(PIPESRC_WIDTH_MASK, tmp) + 1,
		      REG_FIELD_GET(PIPESRC_HEIGHT_MASK, tmp) + 1);

	intel_joiner_adjust_pipe_src(pipe_config);
}

void i9xx_set_pipeconf(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val = 0;

	/*
	 * - We keep both pipes enabled on 830
	 * - During modeset the pipe is still disabled and must remain so
	 * - During fastset the pipe is already enabled and must remain so
	 */
	if (display->platform.i830 || !intel_crtc_needs_modeset(crtc_state))
		val |= TRANSCONF_ENABLE;

	if (crtc_state->double_wide)
		val |= TRANSCONF_DOUBLE_WIDE;

	/* only g4x and later have fancy bpc/dither controls */
	if (display->platform.g4x || display->platform.valleyview ||
	    display->platform.cherryview) {
		/* Bspec claims that we can't use dithering for 30bpp pipes. */
		if (crtc_state->dither && crtc_state->pipe_bpp != 30)
			val |= TRANSCONF_DITHER_EN |
				TRANSCONF_DITHER_TYPE_SP;

		switch (crtc_state->pipe_bpp) {
		default:
			/* Case prevented by intel_choose_pipe_bpp_dither. */
			MISSING_CASE(crtc_state->pipe_bpp);
			fallthrough;
		case 18:
			val |= TRANSCONF_BPC_6;
			break;
		case 24:
			val |= TRANSCONF_BPC_8;
			break;
		case 30:
			val |= TRANSCONF_BPC_10;
			break;
		}
	}

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE) {
		if (DISPLAY_VER(display) < 4 ||
		    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO))
			val |= TRANSCONF_INTERLACE_W_FIELD_INDICATION;
		else
			val |= TRANSCONF_INTERLACE_W_SYNC_SHIFT;
	} else {
		val |= TRANSCONF_INTERLACE_PROGRESSIVE;
	}

	if ((display->platform.valleyview || display->platform.cherryview) &&
	    crtc_state->limited_color_range)
		val |= TRANSCONF_COLOR_RANGE_SELECT;

	val |= TRANSCONF_GAMMA_MODE(crtc_state->gamma_mode);

	if (crtc_state->wgc_enable)
		val |= TRANSCONF_WGC_ENABLE;

	val |= TRANSCONF_FRAME_START_DELAY(crtc_state->framestart_delay - 1);

	intel_de_write(display, TRANSCONF(display, cpu_transcoder), val);
	intel_de_posting_read(display, TRANSCONF(display, cpu_transcoder));
}

static enum intel_output_format
bdw_get_pipe_misc_output_format(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	u32 tmp;

	tmp = intel_de_read(display, PIPE_MISC(crtc->pipe));

	if (tmp & PIPE_MISC_YUV420_ENABLE) {
		/*
		 * We support 4:2:0 in full blend mode only.
		 * For xe3_lpd+ this is implied in YUV420 Enable bit.
		 * Ensure the same for prior platforms in YUV420 Mode bit.
		 */
		if (DISPLAY_VER(display) < 30)
			drm_WARN_ON(display->drm,
				    (tmp & PIPE_MISC_YUV420_MODE_FULL_BLEND) == 0);

		return INTEL_OUTPUT_FORMAT_YCBCR420;
	} else if (tmp & PIPE_MISC_OUTPUT_COLORSPACE_YUV) {
		return INTEL_OUTPUT_FORMAT_YCBCR444;
	} else {
		return INTEL_OUTPUT_FORMAT_RGB;
	}
}

static bool i9xx_get_pipe_config(struct intel_crtc *crtc,
				 struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(crtc);
	enum intel_display_power_domain power_domain;
	enum transcoder cpu_transcoder = (enum transcoder)crtc->pipe;
	intel_wakeref_t wakeref;
	bool ret = false;
	u32 tmp;

	power_domain = POWER_DOMAIN_PIPE(crtc->pipe);
	wakeref = intel_display_power_get_if_enabled(display, power_domain);
	if (!wakeref)
		return false;

	tmp = intel_de_read(display, TRANSCONF(display, cpu_transcoder));
	if (!(tmp & TRANSCONF_ENABLE))
		goto out;

	pipe_config->cpu_transcoder = cpu_transcoder;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->sink_format = pipe_config->output_format;

	if (display->platform.g4x || display->platform.valleyview ||
	    display->platform.cherryview) {
		switch (tmp & TRANSCONF_BPC_MASK) {
		case TRANSCONF_BPC_6:
			pipe_config->pipe_bpp = 18;
			break;
		case TRANSCONF_BPC_8:
			pipe_config->pipe_bpp = 24;
			break;
		case TRANSCONF_BPC_10:
			pipe_config->pipe_bpp = 30;
			break;
		default:
			MISSING_CASE(tmp);
			break;
		}
	}

	if ((display->platform.valleyview || display->platform.cherryview) &&
	    (tmp & TRANSCONF_COLOR_RANGE_SELECT))
		pipe_config->limited_color_range = true;

	pipe_config->gamma_mode = REG_FIELD_GET(TRANSCONF_GAMMA_MODE_MASK_I9XX, tmp);

	pipe_config->framestart_delay = REG_FIELD_GET(TRANSCONF_FRAME_START_DELAY_MASK, tmp) + 1;

	if ((display->platform.valleyview || display->platform.cherryview) &&
	    (tmp & TRANSCONF_WGC_ENABLE))
		pipe_config->wgc_enable = true;

	intel_color_get_config(pipe_config);

	if (HAS_DOUBLE_WIDE(display))
		pipe_config->double_wide = tmp & TRANSCONF_DOUBLE_WIDE;

	intel_get_transcoder_timings(crtc, pipe_config);
	intel_get_pipe_src_size(crtc, pipe_config);

	i9xx_pfit_get_config(pipe_config);

	i9xx_dpll_get_hw_state(crtc, &pipe_config->dpll_hw_state);

	if (DISPLAY_VER(display) >= 4) {
		tmp = pipe_config->dpll_hw_state.i9xx.dpll_md;
		pipe_config->pixel_multiplier =
			((tmp & DPLL_MD_UDI_MULTIPLIER_MASK)
			 >> DPLL_MD_UDI_MULTIPLIER_SHIFT) + 1;
	} else if (display->platform.i945g || display->platform.i945gm ||
		   display->platform.g33 || display->platform.pineview) {
		tmp = pipe_config->dpll_hw_state.i9xx.dpll;
		pipe_config->pixel_multiplier =
			((tmp & SDVO_MULTIPLIER_MASK)
			 >> SDVO_MULTIPLIER_SHIFT_HIRES) + 1;
	} else {
		/* Note that on i915G/GM the pixel multiplier is in the sdvo
		 * port and will be fixed up in the encoder->get_config
		 * function. */
		pipe_config->pixel_multiplier = 1;
	}

	if (display->platform.cherryview)
		chv_crtc_clock_get(pipe_config);
	else if (display->platform.valleyview)
		vlv_crtc_clock_get(pipe_config);
	else
		i9xx_crtc_clock_get(pipe_config);

	/*
	 * Normally the dotclock is filled in by the encoder .get_config()
	 * but in case the pipe is enabled w/o any ports we need a sane
	 * default.
	 */
	pipe_config->hw.adjusted_mode.crtc_clock =
		pipe_config->port_clock / pipe_config->pixel_multiplier;

	ret = true;

out:
	intel_display_power_put(display, power_domain, wakeref);

	return ret;
}

void ilk_set_pipeconf(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val = 0;

	/*
	 * - During modeset the pipe is still disabled and must remain so
	 * - During fastset the pipe is already enabled and must remain so
	 */
	if (!intel_crtc_needs_modeset(crtc_state))
		val |= TRANSCONF_ENABLE;

	switch (crtc_state->pipe_bpp) {
	default:
		/* Case prevented by intel_choose_pipe_bpp_dither. */
		MISSING_CASE(crtc_state->pipe_bpp);
		fallthrough;
	case 18:
		val |= TRANSCONF_BPC_6;
		break;
	case 24:
		val |= TRANSCONF_BPC_8;
		break;
	case 30:
		val |= TRANSCONF_BPC_10;
		break;
	case 36:
		val |= TRANSCONF_BPC_12;
		break;
	}

	if (crtc_state->dither)
		val |= TRANSCONF_DITHER_EN | TRANSCONF_DITHER_TYPE_SP;

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= TRANSCONF_INTERLACE_IF_ID_ILK;
	else
		val |= TRANSCONF_INTERLACE_PF_PD_ILK;

	/*
	 * This would end up with an odd purple hue over
	 * the entire display. Make sure we don't do it.
	 */
	drm_WARN_ON(display->drm, crtc_state->limited_color_range &&
		    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB);

	if (crtc_state->limited_color_range &&
	    !intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO))
		val |= TRANSCONF_COLOR_RANGE_SELECT;

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB)
		val |= TRANSCONF_OUTPUT_COLORSPACE_YUV709;

	val |= TRANSCONF_GAMMA_MODE(crtc_state->gamma_mode);

	val |= TRANSCONF_FRAME_START_DELAY(crtc_state->framestart_delay - 1);
	val |= TRANSCONF_MSA_TIMING_DELAY(crtc_state->msa_timing_delay);

	intel_de_write(display, TRANSCONF(display, cpu_transcoder), val);
	intel_de_posting_read(display, TRANSCONF(display, cpu_transcoder));
}

static void hsw_set_transconf(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val = 0;

	/*
	 * - During modeset the pipe is still disabled and must remain so
	 * - During fastset the pipe is already enabled and must remain so
	 */
	if (!intel_crtc_needs_modeset(crtc_state))
		val |= TRANSCONF_ENABLE;

	if (display->platform.haswell && crtc_state->dither)
		val |= TRANSCONF_DITHER_EN | TRANSCONF_DITHER_TYPE_SP;

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= TRANSCONF_INTERLACE_IF_ID_ILK;
	else
		val |= TRANSCONF_INTERLACE_PF_PD_ILK;

	if (display->platform.haswell &&
	    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB)
		val |= TRANSCONF_OUTPUT_COLORSPACE_YUV_HSW;

	intel_de_write(display, TRANSCONF(display, cpu_transcoder), val);
	intel_de_posting_read(display, TRANSCONF(display, cpu_transcoder));
}

static void bdw_set_pipe_misc(struct intel_dsb *dsb,
			      const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 val = 0;

	switch (crtc_state->pipe_bpp) {
	case 18:
		val |= PIPE_MISC_BPC_6;
		break;
	case 24:
		val |= PIPE_MISC_BPC_8;
		break;
	case 30:
		val |= PIPE_MISC_BPC_10;
		break;
	case 36:
		/* Port output 12BPC defined for ADLP+ */
		if (DISPLAY_VER(display) >= 13)
			val |= PIPE_MISC_BPC_12_ADLP;
		break;
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		break;
	}

	if (crtc_state->dither)
		val |= PIPE_MISC_DITHER_ENABLE | PIPE_MISC_DITHER_TYPE_SP;

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 ||
	    crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		val |= PIPE_MISC_OUTPUT_COLORSPACE_YUV;

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		val |= DISPLAY_VER(display) >= 30 ? PIPE_MISC_YUV420_ENABLE :
			PIPE_MISC_YUV420_ENABLE | PIPE_MISC_YUV420_MODE_FULL_BLEND;

	if (DISPLAY_VER(display) >= 11 && is_hdr_mode(crtc_state))
		val |= PIPE_MISC_HDR_MODE_PRECISION;

	if (DISPLAY_VER(display) >= 12)
		val |= PIPE_MISC_PIXEL_ROUNDING_TRUNC;

	/* allow PSR with sprite enabled */
	if (display->platform.broadwell)
		val |= PIPE_MISC_PSR_MASK_SPRITE_ENABLE;

	intel_de_write_dsb(display, dsb, PIPE_MISC(crtc->pipe), val);
}

int bdw_get_pipe_misc_bpp(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	u32 tmp;

	tmp = intel_de_read(display, PIPE_MISC(crtc->pipe));

	switch (tmp & PIPE_MISC_BPC_MASK) {
	case PIPE_MISC_BPC_6:
		return 18;
	case PIPE_MISC_BPC_8:
		return 24;
	case PIPE_MISC_BPC_10:
		return 30;
	/*
	 * PORT OUTPUT 12 BPC defined for ADLP+.
	 *
	 * TODO:
	 * For previous platforms with DSI interface, bits 5:7
	 * are used for storing pipe_bpp irrespective of dithering.
	 * Since the value of 12 BPC is not defined for these bits
	 * on older platforms, need to find a workaround for 12 BPC
	 * MIPI DSI HW readout.
	 */
	case PIPE_MISC_BPC_12_ADLP:
		if (DISPLAY_VER(display) >= 13)
			return 36;
		fallthrough;
	default:
		MISSING_CASE(tmp);
		return 0;
	}
}

int ilk_get_lanes_required(int target_clock, int link_bw, int bpp)
{
	/*
	 * Account for spread spectrum to avoid
	 * oversubscribing the link. Max center spread
	 * is 2.5%; use 5% for safety's sake.
	 */
	u32 bps = target_clock * bpp * 21 / 20;
	return DIV_ROUND_UP(bps, link_bw * 8);
}

void intel_get_m_n(struct intel_display *display,
		   struct intel_link_m_n *m_n,
		   i915_reg_t data_m_reg, i915_reg_t data_n_reg,
		   i915_reg_t link_m_reg, i915_reg_t link_n_reg)
{
	m_n->link_m = intel_de_read(display, link_m_reg) & DATA_LINK_M_N_MASK;
	m_n->link_n = intel_de_read(display, link_n_reg) & DATA_LINK_M_N_MASK;
	m_n->data_m = intel_de_read(display, data_m_reg) & DATA_LINK_M_N_MASK;
	m_n->data_n = intel_de_read(display, data_n_reg) & DATA_LINK_M_N_MASK;
	m_n->tu = REG_FIELD_GET(TU_SIZE_MASK, intel_de_read(display, data_m_reg)) + 1;
}

void intel_cpu_transcoder_get_m1_n1(struct intel_crtc *crtc,
				    enum transcoder transcoder,
				    struct intel_link_m_n *m_n)
{
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;

	if (DISPLAY_VER(display) >= 5)
		intel_get_m_n(display, m_n,
			      PIPE_DATA_M1(display, transcoder),
			      PIPE_DATA_N1(display, transcoder),
			      PIPE_LINK_M1(display, transcoder),
			      PIPE_LINK_N1(display, transcoder));
	else
		intel_get_m_n(display, m_n,
			      PIPE_DATA_M_G4X(pipe), PIPE_DATA_N_G4X(pipe),
			      PIPE_LINK_M_G4X(pipe), PIPE_LINK_N_G4X(pipe));
}

void intel_cpu_transcoder_get_m2_n2(struct intel_crtc *crtc,
				    enum transcoder transcoder,
				    struct intel_link_m_n *m_n)
{
	struct intel_display *display = to_intel_display(crtc);

	if (!intel_cpu_transcoder_has_m2_n2(display, transcoder))
		return;

	intel_get_m_n(display, m_n,
		      PIPE_DATA_M2(display, transcoder),
		      PIPE_DATA_N2(display, transcoder),
		      PIPE_LINK_M2(display, transcoder),
		      PIPE_LINK_N2(display, transcoder));
}

static bool ilk_get_pipe_config(struct intel_crtc *crtc,
				struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(crtc);
	enum intel_display_power_domain power_domain;
	enum transcoder cpu_transcoder = (enum transcoder)crtc->pipe;
	intel_wakeref_t wakeref;
	bool ret = false;
	u32 tmp;

	power_domain = POWER_DOMAIN_PIPE(crtc->pipe);
	wakeref = intel_display_power_get_if_enabled(display, power_domain);
	if (!wakeref)
		return false;

	tmp = intel_de_read(display, TRANSCONF(display, cpu_transcoder));
	if (!(tmp & TRANSCONF_ENABLE))
		goto out;

	pipe_config->cpu_transcoder = cpu_transcoder;

	switch (tmp & TRANSCONF_BPC_MASK) {
	case TRANSCONF_BPC_6:
		pipe_config->pipe_bpp = 18;
		break;
	case TRANSCONF_BPC_8:
		pipe_config->pipe_bpp = 24;
		break;
	case TRANSCONF_BPC_10:
		pipe_config->pipe_bpp = 30;
		break;
	case TRANSCONF_BPC_12:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}

	if (tmp & TRANSCONF_COLOR_RANGE_SELECT)
		pipe_config->limited_color_range = true;

	switch (tmp & TRANSCONF_OUTPUT_COLORSPACE_MASK) {
	case TRANSCONF_OUTPUT_COLORSPACE_YUV601:
	case TRANSCONF_OUTPUT_COLORSPACE_YUV709:
		pipe_config->output_format = INTEL_OUTPUT_FORMAT_YCBCR444;
		break;
	default:
		pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
		break;
	}

	pipe_config->sink_format = pipe_config->output_format;

	pipe_config->gamma_mode = REG_FIELD_GET(TRANSCONF_GAMMA_MODE_MASK_ILK, tmp);

	pipe_config->framestart_delay = REG_FIELD_GET(TRANSCONF_FRAME_START_DELAY_MASK, tmp) + 1;

	pipe_config->msa_timing_delay = REG_FIELD_GET(TRANSCONF_MSA_TIMING_DELAY_MASK, tmp);

	intel_color_get_config(pipe_config);

	pipe_config->pixel_multiplier = 1;

	ilk_pch_get_config(pipe_config);

	intel_get_transcoder_timings(crtc, pipe_config);
	intel_get_pipe_src_size(crtc, pipe_config);

	ilk_pfit_get_config(pipe_config);

	ret = true;

out:
	intel_display_power_put(display, power_domain, wakeref);

	return ret;
}

static u8 joiner_pipes(struct intel_display *display)
{
	u8 pipes;

	if (DISPLAY_VER(display) >= 12)
		pipes = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D);
	else if (DISPLAY_VER(display) >= 11)
		pipes = BIT(PIPE_B) | BIT(PIPE_C);
	else
		pipes = 0;

	return pipes & DISPLAY_RUNTIME_INFO(display)->pipe_mask;
}

static bool transcoder_ddi_func_is_enabled(struct intel_display *display,
					   enum transcoder cpu_transcoder)
{
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	u32 tmp = 0;

	power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);

	with_intel_display_power_if_enabled(display, power_domain, wakeref)
		tmp = intel_de_read(display,
				    TRANS_DDI_FUNC_CTL(display, cpu_transcoder));

	return tmp & TRANS_DDI_FUNC_ENABLE;
}

static void enabled_uncompressed_joiner_pipes(struct intel_display *display,
					      u8 *primary_pipes, u8 *secondary_pipes)
{
	struct intel_crtc *crtc;

	*primary_pipes = 0;
	*secondary_pipes = 0;

	if (!HAS_UNCOMPRESSED_JOINER(display))
		return;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc,
					 joiner_pipes(display)) {
		enum intel_display_power_domain power_domain;
		enum pipe pipe = crtc->pipe;
		intel_wakeref_t wakeref;

		power_domain = POWER_DOMAIN_PIPE(pipe);
		with_intel_display_power_if_enabled(display, power_domain, wakeref) {
			u32 tmp = intel_de_read(display, ICL_PIPE_DSS_CTL1(pipe));

			if (tmp & UNCOMPRESSED_JOINER_PRIMARY)
				*primary_pipes |= BIT(pipe);
			if (tmp & UNCOMPRESSED_JOINER_SECONDARY)
				*secondary_pipes |= BIT(pipe);
		}
	}
}

static void enabled_bigjoiner_pipes(struct intel_display *display,
				    u8 *primary_pipes, u8 *secondary_pipes)
{
	struct intel_crtc *crtc;

	*primary_pipes = 0;
	*secondary_pipes = 0;

	if (!HAS_BIGJOINER(display))
		return;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc,
					 joiner_pipes(display)) {
		enum intel_display_power_domain power_domain;
		enum pipe pipe = crtc->pipe;
		intel_wakeref_t wakeref;

		power_domain = intel_dsc_power_domain(crtc, (enum transcoder)pipe);
		with_intel_display_power_if_enabled(display, power_domain, wakeref) {
			u32 tmp = intel_de_read(display, ICL_PIPE_DSS_CTL1(pipe));

			if (!(tmp & BIG_JOINER_ENABLE))
				continue;

			if (tmp & PRIMARY_BIG_JOINER_ENABLE)
				*primary_pipes |= BIT(pipe);
			else
				*secondary_pipes |= BIT(pipe);
		}
	}
}

static u8 expected_secondary_pipes(u8 primary_pipes, int num_pipes)
{
	u8 secondary_pipes = 0;

	for (int i = 1; i < num_pipes; i++)
		secondary_pipes |= primary_pipes << i;

	return secondary_pipes;
}

static u8 expected_uncompressed_joiner_secondary_pipes(u8 uncompjoiner_primary_pipes)
{
	return expected_secondary_pipes(uncompjoiner_primary_pipes, 2);
}

static u8 expected_bigjoiner_secondary_pipes(u8 bigjoiner_primary_pipes)
{
	return expected_secondary_pipes(bigjoiner_primary_pipes, 2);
}

static u8 get_joiner_primary_pipe(enum pipe pipe, u8 primary_pipes)
{
	primary_pipes &= GENMASK(pipe, 0);

	return primary_pipes ? BIT(fls(primary_pipes) - 1) : 0;
}

static u8 expected_ultrajoiner_secondary_pipes(u8 ultrajoiner_primary_pipes)
{
	return expected_secondary_pipes(ultrajoiner_primary_pipes, 4);
}

static u8 fixup_ultrajoiner_secondary_pipes(u8 ultrajoiner_primary_pipes,
					    u8 ultrajoiner_secondary_pipes)
{
	return ultrajoiner_secondary_pipes | ultrajoiner_primary_pipes << 3;
}

static void enabled_ultrajoiner_pipes(struct intel_display *display,
				      u8 *primary_pipes, u8 *secondary_pipes)
{
	struct intel_crtc *crtc;

	*primary_pipes = 0;
	*secondary_pipes = 0;

	if (!HAS_ULTRAJOINER(display))
		return;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc,
					 joiner_pipes(display)) {
		enum intel_display_power_domain power_domain;
		enum pipe pipe = crtc->pipe;
		intel_wakeref_t wakeref;

		power_domain = intel_dsc_power_domain(crtc, (enum transcoder)pipe);
		with_intel_display_power_if_enabled(display, power_domain, wakeref) {
			u32 tmp = intel_de_read(display, ICL_PIPE_DSS_CTL1(pipe));

			if (!(tmp & ULTRA_JOINER_ENABLE))
				continue;

			if (tmp & PRIMARY_ULTRA_JOINER_ENABLE)
				*primary_pipes |= BIT(pipe);
			else
				*secondary_pipes |= BIT(pipe);
		}
	}
}

static void enabled_joiner_pipes(struct intel_display *display,
				 enum pipe pipe,
				 u8 *primary_pipe, u8 *secondary_pipes)
{
	u8 primary_ultrajoiner_pipes;
	u8 primary_uncompressed_joiner_pipes, primary_bigjoiner_pipes;
	u8 secondary_ultrajoiner_pipes;
	u8 secondary_uncompressed_joiner_pipes, secondary_bigjoiner_pipes;
	u8 ultrajoiner_pipes;
	u8 uncompressed_joiner_pipes, bigjoiner_pipes;

	enabled_ultrajoiner_pipes(display, &primary_ultrajoiner_pipes,
				  &secondary_ultrajoiner_pipes);
	/*
	 * For some strange reason the last pipe in the set of four
	 * shouldn't have ultrajoiner enable bit set in hardware.
	 * Set the bit anyway to make life easier.
	 */
	drm_WARN_ON(display->drm,
		    expected_secondary_pipes(primary_ultrajoiner_pipes, 3) !=
		    secondary_ultrajoiner_pipes);
	secondary_ultrajoiner_pipes =
		fixup_ultrajoiner_secondary_pipes(primary_ultrajoiner_pipes,
						  secondary_ultrajoiner_pipes);

	drm_WARN_ON(display->drm, (primary_ultrajoiner_pipes & secondary_ultrajoiner_pipes) != 0);

	enabled_uncompressed_joiner_pipes(display, &primary_uncompressed_joiner_pipes,
					  &secondary_uncompressed_joiner_pipes);

	drm_WARN_ON(display->drm,
		    (primary_uncompressed_joiner_pipes & secondary_uncompressed_joiner_pipes) != 0);

	enabled_bigjoiner_pipes(display, &primary_bigjoiner_pipes,
				&secondary_bigjoiner_pipes);

	drm_WARN_ON(display->drm,
		    (primary_bigjoiner_pipes & secondary_bigjoiner_pipes) != 0);

	ultrajoiner_pipes = primary_ultrajoiner_pipes | secondary_ultrajoiner_pipes;
	uncompressed_joiner_pipes = primary_uncompressed_joiner_pipes |
				    secondary_uncompressed_joiner_pipes;
	bigjoiner_pipes = primary_bigjoiner_pipes | secondary_bigjoiner_pipes;

	drm_WARN(display->drm, (ultrajoiner_pipes & bigjoiner_pipes) != ultrajoiner_pipes,
		 "Ultrajoiner pipes(%#x) should be bigjoiner pipes(%#x)\n",
		 ultrajoiner_pipes, bigjoiner_pipes);

	drm_WARN(display->drm, secondary_ultrajoiner_pipes !=
		 expected_ultrajoiner_secondary_pipes(primary_ultrajoiner_pipes),
		 "Wrong secondary ultrajoiner pipes(expected %#x, current %#x)\n",
		 expected_ultrajoiner_secondary_pipes(primary_ultrajoiner_pipes),
		 secondary_ultrajoiner_pipes);

	drm_WARN(display->drm, (uncompressed_joiner_pipes & bigjoiner_pipes) != 0,
		 "Uncompressed joiner pipes(%#x) and bigjoiner pipes(%#x) can't intersect\n",
		 uncompressed_joiner_pipes, bigjoiner_pipes);

	drm_WARN(display->drm, secondary_bigjoiner_pipes !=
		 expected_bigjoiner_secondary_pipes(primary_bigjoiner_pipes),
		 "Wrong secondary bigjoiner pipes(expected %#x, current %#x)\n",
		 expected_bigjoiner_secondary_pipes(primary_bigjoiner_pipes),
		 secondary_bigjoiner_pipes);

	drm_WARN(display->drm, secondary_uncompressed_joiner_pipes !=
		 expected_uncompressed_joiner_secondary_pipes(primary_uncompressed_joiner_pipes),
		 "Wrong secondary uncompressed joiner pipes(expected %#x, current %#x)\n",
		 expected_uncompressed_joiner_secondary_pipes(primary_uncompressed_joiner_pipes),
		 secondary_uncompressed_joiner_pipes);

	*primary_pipe = 0;
	*secondary_pipes = 0;

	if (ultrajoiner_pipes & BIT(pipe)) {
		*primary_pipe = get_joiner_primary_pipe(pipe, primary_ultrajoiner_pipes);
		*secondary_pipes = secondary_ultrajoiner_pipes &
				   expected_ultrajoiner_secondary_pipes(*primary_pipe);

		drm_WARN(display->drm,
			 expected_ultrajoiner_secondary_pipes(*primary_pipe) !=
			 *secondary_pipes,
			 "Wrong ultrajoiner secondary pipes for primary_pipe %#x (expected %#x, current %#x)\n",
			 *primary_pipe,
			 expected_ultrajoiner_secondary_pipes(*primary_pipe),
			 *secondary_pipes);
		return;
	}

	if (uncompressed_joiner_pipes & BIT(pipe)) {
		*primary_pipe = get_joiner_primary_pipe(pipe, primary_uncompressed_joiner_pipes);
		*secondary_pipes = secondary_uncompressed_joiner_pipes &
				   expected_uncompressed_joiner_secondary_pipes(*primary_pipe);

		drm_WARN(display->drm,
			 expected_uncompressed_joiner_secondary_pipes(*primary_pipe) !=
			 *secondary_pipes,
			 "Wrong uncompressed joiner secondary pipes for primary_pipe %#x (expected %#x, current %#x)\n",
			 *primary_pipe,
			 expected_uncompressed_joiner_secondary_pipes(*primary_pipe),
			 *secondary_pipes);
		return;
	}

	if (bigjoiner_pipes & BIT(pipe)) {
		*primary_pipe = get_joiner_primary_pipe(pipe, primary_bigjoiner_pipes);
		*secondary_pipes = secondary_bigjoiner_pipes &
				   expected_bigjoiner_secondary_pipes(*primary_pipe);

		drm_WARN(display->drm,
			 expected_bigjoiner_secondary_pipes(*primary_pipe) !=
			 *secondary_pipes,
			 "Wrong bigjoiner secondary pipes for primary_pipe %#x (expected %#x, current %#x)\n",
			 *primary_pipe,
			 expected_bigjoiner_secondary_pipes(*primary_pipe),
			 *secondary_pipes);
		return;
	}
}

static u8 hsw_panel_transcoders(struct intel_display *display)
{
	u8 panel_transcoder_mask = BIT(TRANSCODER_EDP);

	if (DISPLAY_VER(display) >= 11)
		panel_transcoder_mask |= BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1);

	return panel_transcoder_mask;
}

static u8 hsw_enabled_transcoders(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	u8 panel_transcoder_mask = hsw_panel_transcoders(display);
	enum transcoder cpu_transcoder;
	u8 primary_pipe, secondary_pipes;
	u8 enabled_transcoders = 0;

	/*
	 * XXX: Do intel_display_power_get_if_enabled before reading this (for
	 * consistency and less surprising code; it's in always on power).
	 */
	for_each_cpu_transcoder_masked(display, cpu_transcoder,
				       panel_transcoder_mask) {
		enum intel_display_power_domain power_domain;
		intel_wakeref_t wakeref;
		enum pipe trans_pipe;
		u32 tmp = 0;

		power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);
		with_intel_display_power_if_enabled(display, power_domain, wakeref)
			tmp = intel_de_read(display,
					    TRANS_DDI_FUNC_CTL(display, cpu_transcoder));

		if (!(tmp & TRANS_DDI_FUNC_ENABLE))
			continue;

		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		default:
			drm_WARN(display->drm, 1,
				 "unknown pipe linked to transcoder %s\n",
				 transcoder_name(cpu_transcoder));
			fallthrough;
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
		case TRANS_DDI_EDP_INPUT_A_ON:
			trans_pipe = PIPE_A;
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			trans_pipe = PIPE_B;
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			trans_pipe = PIPE_C;
			break;
		case TRANS_DDI_EDP_INPUT_D_ONOFF:
			trans_pipe = PIPE_D;
			break;
		}

		if (trans_pipe == crtc->pipe)
			enabled_transcoders |= BIT(cpu_transcoder);
	}

	/* single pipe or joiner primary */
	cpu_transcoder = (enum transcoder) crtc->pipe;
	if (transcoder_ddi_func_is_enabled(display, cpu_transcoder))
		enabled_transcoders |= BIT(cpu_transcoder);

	/* joiner secondary -> consider the primary pipe's transcoder as well */
	enabled_joiner_pipes(display, crtc->pipe, &primary_pipe, &secondary_pipes);
	if (secondary_pipes & BIT(crtc->pipe)) {
		cpu_transcoder = (enum transcoder)ffs(primary_pipe) - 1;
		if (transcoder_ddi_func_is_enabled(display, cpu_transcoder))
			enabled_transcoders |= BIT(cpu_transcoder);
	}

	return enabled_transcoders;
}

static bool has_edp_transcoders(u8 enabled_transcoders)
{
	return enabled_transcoders & BIT(TRANSCODER_EDP);
}

static bool has_dsi_transcoders(u8 enabled_transcoders)
{
	return enabled_transcoders & (BIT(TRANSCODER_DSI_0) |
				      BIT(TRANSCODER_DSI_1));
}

static bool has_pipe_transcoders(u8 enabled_transcoders)
{
	return enabled_transcoders & ~(BIT(TRANSCODER_EDP) |
				       BIT(TRANSCODER_DSI_0) |
				       BIT(TRANSCODER_DSI_1));
}

static void assert_enabled_transcoders(struct intel_display *display,
				       u8 enabled_transcoders)
{
	/* Only one type of transcoder please */
	drm_WARN_ON(display->drm,
		    has_edp_transcoders(enabled_transcoders) +
		    has_dsi_transcoders(enabled_transcoders) +
		    has_pipe_transcoders(enabled_transcoders) > 1);

	/* Only DSI transcoders can be ganged */
	drm_WARN_ON(display->drm,
		    !has_dsi_transcoders(enabled_transcoders) &&
		    !is_power_of_2(enabled_transcoders));
}

static bool hsw_get_transcoder_state(struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config,
				     struct intel_display_power_domain_set *power_domain_set)
{
	struct intel_display *display = to_intel_display(crtc);
	unsigned long enabled_transcoders;
	u32 tmp;

	enabled_transcoders = hsw_enabled_transcoders(crtc);
	if (!enabled_transcoders)
		return false;

	assert_enabled_transcoders(display, enabled_transcoders);

	/*
	 * With the exception of DSI we should only ever have
	 * a single enabled transcoder. With DSI let's just
	 * pick the first one.
	 */
	pipe_config->cpu_transcoder = ffs(enabled_transcoders) - 1;

	if (!intel_display_power_get_in_set_if_enabled(display, power_domain_set,
						       POWER_DOMAIN_TRANSCODER(pipe_config->cpu_transcoder)))
		return false;

	if (hsw_panel_transcoders(display) & BIT(pipe_config->cpu_transcoder)) {
		tmp = intel_de_read(display,
				    TRANS_DDI_FUNC_CTL(display, pipe_config->cpu_transcoder));

		if ((tmp & TRANS_DDI_EDP_INPUT_MASK) == TRANS_DDI_EDP_INPUT_A_ONOFF)
			pipe_config->pch_pfit.force_thru = true;
	}

	tmp = intel_de_read(display,
			    TRANSCONF(display, pipe_config->cpu_transcoder));

	return tmp & TRANSCONF_ENABLE;
}

static bool bxt_get_dsi_transcoder_state(struct intel_crtc *crtc,
					 struct intel_crtc_state *pipe_config,
					 struct intel_display_power_domain_set *power_domain_set)
{
	struct intel_display *display = to_intel_display(crtc);
	enum transcoder cpu_transcoder;
	enum port port;
	u32 tmp;

	for_each_port_masked(port, BIT(PORT_A) | BIT(PORT_C)) {
		if (port == PORT_A)
			cpu_transcoder = TRANSCODER_DSI_A;
		else
			cpu_transcoder = TRANSCODER_DSI_C;

		if (!intel_display_power_get_in_set_if_enabled(display, power_domain_set,
							       POWER_DOMAIN_TRANSCODER(cpu_transcoder)))
			continue;

		/*
		 * The PLL needs to be enabled with a valid divider
		 * configuration, otherwise accessing DSI registers will hang
		 * the machine. See BSpec North Display Engine
		 * registers/MIPI[BXT]. We can break out here early, since we
		 * need the same DSI PLL to be enabled for both DSI ports.
		 */
		if (!bxt_dsi_pll_is_enabled(display))
			break;

		/* XXX: this works for video mode only */
		tmp = intel_de_read(display, BXT_MIPI_PORT_CTRL(port));
		if (!(tmp & DPI_ENABLE))
			continue;

		tmp = intel_de_read(display, MIPI_CTRL(display, port));
		if ((tmp & BXT_PIPE_SELECT_MASK) != BXT_PIPE_SELECT(crtc->pipe))
			continue;

		pipe_config->cpu_transcoder = cpu_transcoder;
		break;
	}

	return transcoder_is_dsi(pipe_config->cpu_transcoder);
}

static void intel_joiner_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u8 primary_pipe, secondary_pipes;
	enum pipe pipe = crtc->pipe;

	enabled_joiner_pipes(display, pipe, &primary_pipe, &secondary_pipes);

	if (((primary_pipe | secondary_pipes) & BIT(pipe)) == 0)
		return;

	crtc_state->joiner_pipes = primary_pipe | secondary_pipes;
}

static bool hsw_get_pipe_config(struct intel_crtc *crtc,
				struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(crtc);
	bool active;
	u32 tmp;

	if (!intel_display_power_get_in_set_if_enabled(display, &crtc->hw_readout_power_domains,
						       POWER_DOMAIN_PIPE(crtc->pipe)))
		return false;

	active = hsw_get_transcoder_state(crtc, pipe_config, &crtc->hw_readout_power_domains);

	if ((display->platform.geminilake || display->platform.broxton) &&
	    bxt_get_dsi_transcoder_state(crtc, pipe_config, &crtc->hw_readout_power_domains)) {
		drm_WARN_ON(display->drm, active);
		active = true;
	}

	if (!active)
		goto out;

	intel_joiner_get_config(pipe_config);
	intel_dsc_get_config(pipe_config);

	if (!transcoder_is_dsi(pipe_config->cpu_transcoder) ||
	    DISPLAY_VER(display) >= 11)
		intel_get_transcoder_timings(crtc, pipe_config);

	if (transcoder_has_vrr(pipe_config))
		intel_vrr_get_config(pipe_config);

	intel_get_pipe_src_size(crtc, pipe_config);

	if (display->platform.haswell) {
		u32 tmp = intel_de_read(display,
					TRANSCONF(display, pipe_config->cpu_transcoder));

		if (tmp & TRANSCONF_OUTPUT_COLORSPACE_YUV_HSW)
			pipe_config->output_format = INTEL_OUTPUT_FORMAT_YCBCR444;
		else
			pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	} else {
		pipe_config->output_format =
			bdw_get_pipe_misc_output_format(crtc);
	}

	pipe_config->sink_format = pipe_config->output_format;

	intel_color_get_config(pipe_config);

	tmp = intel_de_read(display, WM_LINETIME(crtc->pipe));
	pipe_config->linetime = REG_FIELD_GET(HSW_LINETIME_MASK, tmp);
	if (display->platform.broadwell || display->platform.haswell)
		pipe_config->ips_linetime =
			REG_FIELD_GET(HSW_IPS_LINETIME_MASK, tmp);

	if (intel_display_power_get_in_set_if_enabled(display, &crtc->hw_readout_power_domains,
						      POWER_DOMAIN_PIPE_PANEL_FITTER(crtc->pipe))) {
		if (DISPLAY_VER(display) >= 9)
			skl_scaler_get_config(pipe_config);
		else
			ilk_pfit_get_config(pipe_config);
	}

	hsw_ips_get_config(pipe_config);

	if (pipe_config->cpu_transcoder != TRANSCODER_EDP &&
	    !transcoder_is_dsi(pipe_config->cpu_transcoder)) {
		pipe_config->pixel_multiplier =
			intel_de_read(display,
				      TRANS_MULT(display, pipe_config->cpu_transcoder)) + 1;
	} else {
		pipe_config->pixel_multiplier = 1;
	}

	if (!transcoder_is_dsi(pipe_config->cpu_transcoder)) {
		tmp = intel_de_read(display, CHICKEN_TRANS(display, pipe_config->cpu_transcoder));

		pipe_config->framestart_delay = REG_FIELD_GET(HSW_FRAME_START_DELAY_MASK, tmp) + 1;
	} else {
		/* no idea if this is correct */
		pipe_config->framestart_delay = 1;
	}

out:
	intel_display_power_put_all_in_set(display, &crtc->hw_readout_power_domains);

	return active;
}

bool intel_crtc_get_pipe_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!display->funcs.display->get_pipe_config(crtc, crtc_state))
		return false;

	crtc_state->hw.active = true;

	intel_crtc_readout_derived_state(crtc_state);

	return true;
}

int intel_dotclock_calculate(int link_freq,
			     const struct intel_link_m_n *m_n)
{
	/*
	 * The calculation for the data clock -> pixel clock is:
	 * pixel_clock = ((m/n)*(link_clock * nr_lanes))/bpp
	 * But we want to avoid losing precision if possible, so:
	 * pixel_clock = ((m * link_clock * nr_lanes)/(n*bpp))
	 *
	 * and for link freq (10kbs units) -> pixel clock it is:
	 * link_symbol_clock = link_freq * 10 / link_symbol_size
	 * pixel_clock = (m * link_symbol_clock) / n
	 *    or for more precision:
	 * pixel_clock = (m * link_freq * 10) / (n * link_symbol_size)
	 */

	if (!m_n->link_n)
		return 0;

	return DIV_ROUND_UP_ULL(mul_u32_u32(m_n->link_m, link_freq * 10),
				m_n->link_n * intel_dp_link_symbol_size(link_freq));
}

int intel_crtc_dotclock(const struct intel_crtc_state *pipe_config)
{
	int dotclock;

	if (intel_crtc_has_dp_encoder(pipe_config))
		dotclock = intel_dotclock_calculate(pipe_config->port_clock,
						    &pipe_config->dp_m_n);
	else if (pipe_config->has_hdmi_sink && pipe_config->pipe_bpp > 24)
		dotclock = DIV_ROUND_CLOSEST(pipe_config->port_clock * 24,
					     pipe_config->pipe_bpp);
	else
		dotclock = pipe_config->port_clock;

	if (pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 &&
	    !intel_crtc_has_dp_encoder(pipe_config))
		dotclock *= 2;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	return dotclock;
}

/* Returns the currently programmed mode of the given encoder. */
struct drm_display_mode *
intel_encoder_current_mode(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc_state *crtc_state;
	struct drm_display_mode *mode;
	struct intel_crtc *crtc;
	enum pipe pipe;

	if (!encoder->get_hw_state(encoder, &pipe))
		return NULL;

	crtc = intel_crtc_for_pipe(display, pipe);

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	crtc_state = intel_crtc_state_alloc(crtc);
	if (!crtc_state) {
		kfree(mode);
		return NULL;
	}

	if (!intel_crtc_get_pipe_config(crtc_state)) {
		intel_crtc_destroy_state(&crtc->base, &crtc_state->uapi);
		kfree(mode);
		return NULL;
	}

	intel_encoder_get_config(encoder, crtc_state);

	intel_mode_from_crtc_timings(mode, &crtc_state->hw.adjusted_mode);

	intel_crtc_destroy_state(&crtc->base, &crtc_state->uapi);

	return mode;
}

static bool encoders_cloneable(const struct intel_encoder *a,
			       const struct intel_encoder *b)
{
	/* masks could be asymmetric, so check both ways */
	return a == b || (a->cloneable & BIT(b->type) &&
			  b->cloneable & BIT(a->type));
}

static bool check_single_encoder_cloning(struct intel_atomic_state *state,
					 struct intel_crtc *crtc,
					 struct intel_encoder *encoder)
{
	struct intel_encoder *source_encoder;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	for_each_new_connector_in_state(&state->base, connector, connector_state, i) {
		if (connector_state->crtc != &crtc->base)
			continue;

		source_encoder =
			to_intel_encoder(connector_state->best_encoder);
		if (!encoders_cloneable(encoder, source_encoder))
			return false;
	}

	return true;
}

static u16 hsw_linetime_wm(const struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *pipe_mode =
		&crtc_state->hw.pipe_mode;
	int linetime_wm;

	if (!crtc_state->hw.enable)
		return 0;

	linetime_wm = DIV_ROUND_CLOSEST(pipe_mode->crtc_htotal * 1000 * 8,
					pipe_mode->crtc_clock);

	return min(linetime_wm, 0x1ff);
}

static u16 hsw_ips_linetime_wm(const struct intel_crtc_state *crtc_state,
			       const struct intel_cdclk_state *cdclk_state)
{
	const struct drm_display_mode *pipe_mode =
		&crtc_state->hw.pipe_mode;
	int linetime_wm;

	if (!crtc_state->hw.enable)
		return 0;

	linetime_wm = DIV_ROUND_CLOSEST(pipe_mode->crtc_htotal * 1000 * 8,
					intel_cdclk_logical(cdclk_state));

	return min(linetime_wm, 0x1ff);
}

static u16 skl_linetime_wm(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	const struct drm_display_mode *pipe_mode =
		&crtc_state->hw.pipe_mode;
	int linetime_wm;

	if (!crtc_state->hw.enable)
		return 0;

	linetime_wm = DIV_ROUND_UP(pipe_mode->crtc_htotal * 1000 * 8,
				   crtc_state->pixel_rate);

	/* Display WA #1135: BXT:ALL GLK:ALL */
	if ((display->platform.geminilake || display->platform.broxton) &&
	    skl_watermark_ipc_enabled(display))
		linetime_wm /= 2;

	return min(linetime_wm, 0x1ff);
}

static int hsw_compute_linetime_wm(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_cdclk_state *cdclk_state;

	if (DISPLAY_VER(display) >= 9)
		crtc_state->linetime = skl_linetime_wm(crtc_state);
	else
		crtc_state->linetime = hsw_linetime_wm(crtc_state);

	if (!hsw_crtc_supports_ips(crtc))
		return 0;

	cdclk_state = intel_atomic_get_cdclk_state(state);
	if (IS_ERR(cdclk_state))
		return PTR_ERR(cdclk_state);

	crtc_state->ips_linetime = hsw_ips_linetime_wm(crtc_state,
						       cdclk_state);

	return 0;
}

static int intel_crtc_atomic_check(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	if (DISPLAY_VER(display) < 5 && !display->platform.g4x &&
	    intel_crtc_needs_modeset(crtc_state) &&
	    !crtc_state->hw.active)
		crtc_state->update_wm_post = true;

	if (intel_crtc_needs_modeset(crtc_state)) {
		ret = intel_dpll_crtc_get_dpll(state, crtc);
		if (ret)
			return ret;
	}

	ret = intel_color_check(state, crtc);
	if (ret)
		return ret;

	ret = intel_wm_compute(state, crtc);
	if (ret) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] watermarks are invalid\n",
			    crtc->base.base.id, crtc->base.name);
		return ret;
	}

	if (DISPLAY_VER(display) >= 9) {
		if (intel_crtc_needs_modeset(crtc_state) ||
		    intel_crtc_needs_fastset(crtc_state)) {
			ret = skl_update_scaler_crtc(crtc_state);
			if (ret)
				return ret;
		}

		ret = intel_atomic_setup_scalers(state, crtc);
		if (ret)
			return ret;
	}

	if (HAS_IPS(display)) {
		ret = hsw_ips_compute_config(state, crtc);
		if (ret)
			return ret;
	}

	if (DISPLAY_VER(display) >= 9 ||
	    display->platform.broadwell || display->platform.haswell) {
		ret = hsw_compute_linetime_wm(state, crtc);
		if (ret)
			return ret;

	}

	ret = intel_psr2_sel_fetch_update(state, crtc);
	if (ret)
		return ret;

	return 0;
}

static int
compute_sink_pipe_bpp(const struct drm_connector_state *conn_state,
		      struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	const struct drm_display_info *info = &connector->display_info;
	int bpp;

	switch (conn_state->max_bpc) {
	case 6 ... 7:
		bpp = 6 * 3;
		break;
	case 8 ... 9:
		bpp = 8 * 3;
		break;
	case 10 ... 11:
		bpp = 10 * 3;
		break;
	case 12 ... 16:
		bpp = 12 * 3;
		break;
	default:
		MISSING_CASE(conn_state->max_bpc);
		return -EINVAL;
	}

	if (bpp < crtc_state->pipe_bpp) {
		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] Limiting display bpp to %d "
			    "(EDID bpp %d, max requested bpp %d, max platform bpp %d)\n",
			    connector->base.id, connector->name,
			    bpp, 3 * info->bpc,
			    3 * conn_state->max_requested_bpc,
			    crtc_state->pipe_bpp);

		crtc_state->pipe_bpp = bpp;
	}

	return 0;
}

int intel_display_min_pipe_bpp(void)
{
	return 6 * 3;
}

int intel_display_max_pipe_bpp(struct intel_display *display)
{
	if (display->platform.g4x || display->platform.valleyview ||
	    display->platform.cherryview)
		return 10*3;
	else if (DISPLAY_VER(display) >= 5)
		return 12*3;
	else
		return 8*3;
}

static int
compute_baseline_pipe_bpp(struct intel_atomic_state *state,
			  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	crtc_state->pipe_bpp = intel_display_max_pipe_bpp(display);

	/* Clamp display bpp to connector max bpp */
	for_each_new_connector_in_state(&state->base, connector, connector_state, i) {
		int ret;

		if (connector_state->crtc != &crtc->base)
			continue;

		ret = compute_sink_pipe_bpp(connector_state, crtc_state);
		if (ret)
			return ret;
	}

	return 0;
}

static bool check_digital_port_conflicts(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	unsigned int used_ports = 0;
	unsigned int used_mst_ports = 0;
	bool ret = true;

	/*
	 * We're going to peek into connector->state,
	 * hence connection_mutex must be held.
	 */
	drm_modeset_lock_assert_held(&display->drm->mode_config.connection_mutex);

	/*
	 * Walk the connector list instead of the encoder
	 * list to detect the problem on ddi platforms
	 * where there's just one encoder per digital port.
	 */
	drm_connector_list_iter_begin(display->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *connector_state;
		struct intel_encoder *encoder;

		connector_state =
			drm_atomic_get_new_connector_state(&state->base,
							   connector);
		if (!connector_state)
			connector_state = connector->state;

		if (!connector_state->best_encoder)
			continue;

		encoder = to_intel_encoder(connector_state->best_encoder);

		drm_WARN_ON(display->drm, !connector_state->crtc);

		switch (encoder->type) {
		case INTEL_OUTPUT_DDI:
			if (drm_WARN_ON(display->drm, !HAS_DDI(display)))
				break;
			fallthrough;
		case INTEL_OUTPUT_DP:
		case INTEL_OUTPUT_HDMI:
		case INTEL_OUTPUT_EDP:
			/* the same port mustn't appear more than once */
			if (used_ports & BIT(encoder->port))
				ret = false;

			used_ports |= BIT(encoder->port);
			break;
		case INTEL_OUTPUT_DP_MST:
			used_mst_ports |=
				1 << encoder->port;
			break;
		default:
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	/* can't mix MST and SST/HDMI on the same port */
	if (used_ports & used_mst_ports)
		return false;

	return ret;
}

static void
intel_crtc_copy_uapi_to_hw_state_nomodeset(struct intel_atomic_state *state,
					   struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	WARN_ON(intel_crtc_is_joiner_secondary(crtc_state));

	drm_property_replace_blob(&crtc_state->hw.degamma_lut,
				  crtc_state->uapi.degamma_lut);
	drm_property_replace_blob(&crtc_state->hw.gamma_lut,
				  crtc_state->uapi.gamma_lut);
	drm_property_replace_blob(&crtc_state->hw.ctm,
				  crtc_state->uapi.ctm);
}

static void
intel_crtc_copy_uapi_to_hw_state_modeset(struct intel_atomic_state *state,
					 struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	WARN_ON(intel_crtc_is_joiner_secondary(crtc_state));

	crtc_state->hw.enable = crtc_state->uapi.enable;
	crtc_state->hw.active = crtc_state->uapi.active;
	drm_mode_copy(&crtc_state->hw.mode,
		      &crtc_state->uapi.mode);
	drm_mode_copy(&crtc_state->hw.adjusted_mode,
		      &crtc_state->uapi.adjusted_mode);
	crtc_state->hw.scaling_filter = crtc_state->uapi.scaling_filter;

	intel_crtc_copy_uapi_to_hw_state_nomodeset(state, crtc);
}

static void
copy_joiner_crtc_state_nomodeset(struct intel_atomic_state *state,
				 struct intel_crtc *secondary_crtc)
{
	struct intel_crtc_state *secondary_crtc_state =
		intel_atomic_get_new_crtc_state(state, secondary_crtc);
	struct intel_crtc *primary_crtc = intel_primary_crtc(secondary_crtc_state);
	const struct intel_crtc_state *primary_crtc_state =
		intel_atomic_get_new_crtc_state(state, primary_crtc);

	drm_property_replace_blob(&secondary_crtc_state->hw.degamma_lut,
				  primary_crtc_state->hw.degamma_lut);
	drm_property_replace_blob(&secondary_crtc_state->hw.gamma_lut,
				  primary_crtc_state->hw.gamma_lut);
	drm_property_replace_blob(&secondary_crtc_state->hw.ctm,
				  primary_crtc_state->hw.ctm);

	secondary_crtc_state->uapi.color_mgmt_changed = primary_crtc_state->uapi.color_mgmt_changed;
}

static int
copy_joiner_crtc_state_modeset(struct intel_atomic_state *state,
			       struct intel_crtc *secondary_crtc)
{
	struct intel_crtc_state *secondary_crtc_state =
		intel_atomic_get_new_crtc_state(state, secondary_crtc);
	struct intel_crtc *primary_crtc = intel_primary_crtc(secondary_crtc_state);
	const struct intel_crtc_state *primary_crtc_state =
		intel_atomic_get_new_crtc_state(state, primary_crtc);
	struct intel_crtc_state *saved_state;

	WARN_ON(primary_crtc_state->joiner_pipes !=
		secondary_crtc_state->joiner_pipes);

	saved_state = kmemdup(primary_crtc_state, sizeof(*saved_state), GFP_KERNEL);
	if (!saved_state)
		return -ENOMEM;

	/* preserve some things from the slave's original crtc state */
	saved_state->uapi = secondary_crtc_state->uapi;
	saved_state->scaler_state = secondary_crtc_state->scaler_state;
	saved_state->intel_dpll = secondary_crtc_state->intel_dpll;
	saved_state->crc_enabled = secondary_crtc_state->crc_enabled;

	intel_crtc_free_hw_state(secondary_crtc_state);
	if (secondary_crtc_state->dp_tunnel_ref.tunnel)
		drm_dp_tunnel_ref_put(&secondary_crtc_state->dp_tunnel_ref);
	memcpy(secondary_crtc_state, saved_state, sizeof(*secondary_crtc_state));
	kfree(saved_state);

	/* Re-init hw state */
	memset(&secondary_crtc_state->hw, 0, sizeof(secondary_crtc_state->hw));
	secondary_crtc_state->hw.enable = primary_crtc_state->hw.enable;
	secondary_crtc_state->hw.active = primary_crtc_state->hw.active;
	drm_mode_copy(&secondary_crtc_state->hw.mode,
		      &primary_crtc_state->hw.mode);
	drm_mode_copy(&secondary_crtc_state->hw.pipe_mode,
		      &primary_crtc_state->hw.pipe_mode);
	drm_mode_copy(&secondary_crtc_state->hw.adjusted_mode,
		      &primary_crtc_state->hw.adjusted_mode);
	secondary_crtc_state->hw.scaling_filter = primary_crtc_state->hw.scaling_filter;
	secondary_crtc_state->dpll_hw_state.cx0pll = primary_crtc_state->dpll_hw_state.cx0pll;

	if (primary_crtc_state->dp_tunnel_ref.tunnel)
		drm_dp_tunnel_ref_get(primary_crtc_state->dp_tunnel_ref.tunnel,
				      &secondary_crtc_state->dp_tunnel_ref);

	copy_joiner_crtc_state_nomodeset(state, secondary_crtc);

	secondary_crtc_state->uapi.mode_changed = primary_crtc_state->uapi.mode_changed;
	secondary_crtc_state->uapi.connectors_changed = primary_crtc_state->uapi.connectors_changed;
	secondary_crtc_state->uapi.active_changed = primary_crtc_state->uapi.active_changed;

	WARN_ON(primary_crtc_state->joiner_pipes !=
		secondary_crtc_state->joiner_pipes);

	return 0;
}

static int
intel_crtc_prepare_cleared_state(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc_state *saved_state;

	saved_state = intel_crtc_state_alloc(crtc);
	if (!saved_state)
		return -ENOMEM;

	/* free the old crtc_state->hw members */
	intel_crtc_free_hw_state(crtc_state);

	intel_dp_tunnel_atomic_clear_stream_bw(state, crtc_state);

	/* FIXME: before the switch to atomic started, a new pipe_config was
	 * kzalloc'd. Code that depends on any field being zero should be
	 * fixed, so that the crtc_state can be safely duplicated. For now,
	 * only fields that are know to not cause problems are preserved. */

	saved_state->uapi = crtc_state->uapi;
	saved_state->inherited = crtc_state->inherited;
	saved_state->scaler_state = crtc_state->scaler_state;
	saved_state->intel_dpll = crtc_state->intel_dpll;
	saved_state->dpll_hw_state = crtc_state->dpll_hw_state;
	memcpy(saved_state->icl_port_dplls, crtc_state->icl_port_dplls,
	       sizeof(saved_state->icl_port_dplls));
	saved_state->crc_enabled = crtc_state->crc_enabled;
	if (display->platform.g4x ||
	    display->platform.valleyview || display->platform.cherryview)
		saved_state->wm = crtc_state->wm;

	memcpy(crtc_state, saved_state, sizeof(*crtc_state));
	kfree(saved_state);

	intel_crtc_copy_uapi_to_hw_state_modeset(state, crtc);

	return 0;
}

static int
intel_modeset_pipe_config(struct intel_atomic_state *state,
			  struct intel_crtc *crtc,
			  const struct intel_link_bw_limits *limits)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int pipe_src_w, pipe_src_h;
	int base_bpp, ret, i;

	crtc_state->cpu_transcoder = (enum transcoder) crtc->pipe;

	crtc_state->framestart_delay = 1;

	/*
	 * Sanitize sync polarity flags based on requested ones. If neither
	 * positive or negative polarity is requested, treat this as meaning
	 * negative polarity.
	 */
	if (!(crtc_state->hw.adjusted_mode.flags &
	      (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC)))
		crtc_state->hw.adjusted_mode.flags |= DRM_MODE_FLAG_NHSYNC;

	if (!(crtc_state->hw.adjusted_mode.flags &
	      (DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC)))
		crtc_state->hw.adjusted_mode.flags |= DRM_MODE_FLAG_NVSYNC;

	ret = compute_baseline_pipe_bpp(state, crtc);
	if (ret)
		return ret;

	crtc_state->fec_enable = limits->force_fec_pipes & BIT(crtc->pipe);
	crtc_state->max_link_bpp_x16 = limits->max_bpp_x16[crtc->pipe];

	if (crtc_state->pipe_bpp > fxp_q4_to_int(crtc_state->max_link_bpp_x16)) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] Link bpp limited to " FXP_Q4_FMT "\n",
			    crtc->base.base.id, crtc->base.name,
			    FXP_Q4_ARGS(crtc_state->max_link_bpp_x16));
		crtc_state->bw_constrained = true;
	}

	base_bpp = crtc_state->pipe_bpp;

	/*
	 * Determine the real pipe dimensions. Note that stereo modes can
	 * increase the actual pipe size due to the frame doubling and
	 * insertion of additional space for blanks between the frame. This
	 * is stored in the crtc timings. We use the requested mode to do this
	 * computation to clearly distinguish it from the adjusted mode, which
	 * can be changed by the connectors in the below retry loop.
	 */
	drm_mode_get_hv_timing(&crtc_state->hw.mode,
			       &pipe_src_w, &pipe_src_h);
	drm_rect_init(&crtc_state->pipe_src, 0, 0,
		      pipe_src_w, pipe_src_h);

	for_each_new_connector_in_state(&state->base, connector, connector_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(connector_state->best_encoder);

		if (connector_state->crtc != &crtc->base)
			continue;

		if (!check_single_encoder_cloning(state, crtc, encoder)) {
			drm_dbg_kms(display->drm,
				    "[ENCODER:%d:%s] rejecting invalid cloning configuration\n",
				    encoder->base.base.id, encoder->base.name);
			return -EINVAL;
		}

		/*
		 * Determine output_types before calling the .compute_config()
		 * hooks so that the hooks can use this information safely.
		 */
		if (encoder->compute_output_type)
			crtc_state->output_types |=
				BIT(encoder->compute_output_type(encoder, crtc_state,
								 connector_state));
		else
			crtc_state->output_types |= BIT(encoder->type);
	}

	/* Ensure the port clock defaults are reset when retrying. */
	crtc_state->port_clock = 0;
	crtc_state->pixel_multiplier = 1;

	/* Fill in default crtc timings, allow encoders to overwrite them. */
	drm_mode_set_crtcinfo(&crtc_state->hw.adjusted_mode,
			      CRTC_STEREO_DOUBLE);

	/* Pass our mode to the connectors and the CRTC to give them a chance to
	 * adjust it according to limitations or connector properties, and also
	 * a chance to reject the mode entirely.
	 */
	for_each_new_connector_in_state(&state->base, connector, connector_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(connector_state->best_encoder);

		if (connector_state->crtc != &crtc->base)
			continue;

		ret = encoder->compute_config(encoder, crtc_state,
					      connector_state);
		if (ret == -EDEADLK)
			return ret;
		if (ret < 0) {
			drm_dbg_kms(display->drm, "[ENCODER:%d:%s] config failure: %d\n",
				    encoder->base.base.id, encoder->base.name, ret);
			return ret;
		}
	}

	/* Set default port clock if not overwritten by the encoder. Needs to be
	 * done afterwards in case the encoder adjusts the mode. */
	if (!crtc_state->port_clock)
		crtc_state->port_clock = crtc_state->hw.adjusted_mode.crtc_clock
			* crtc_state->pixel_multiplier;

	ret = intel_crtc_compute_config(state, crtc);
	if (ret == -EDEADLK)
		return ret;
	if (ret < 0) {
		drm_dbg_kms(display->drm, "[CRTC:%d:%s] config failure: %d\n",
			    crtc->base.base.id, crtc->base.name, ret);
		return ret;
	}

	/* Dithering seems to not pass-through bits correctly when it should, so
	 * only enable it on 6bpc panels and when its not a compliance
	 * test requesting 6bpc video pattern.
	 */
	crtc_state->dither = (crtc_state->pipe_bpp == 6*3) &&
		!crtc_state->dither_force_disable;
	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] hw max bpp: %i, pipe bpp: %i, dithering: %i\n",
		    crtc->base.base.id, crtc->base.name,
		    base_bpp, crtc_state->pipe_bpp, crtc_state->dither);

	return 0;
}

static int
intel_modeset_pipe_config_late(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	int i;

	intel_vrr_compute_config_late(crtc_state);

	for_each_new_connector_in_state(&state->base, connector,
					conn_state, i) {
		struct intel_encoder *encoder =
			to_intel_encoder(conn_state->best_encoder);
		int ret;

		if (conn_state->crtc != &crtc->base ||
		    !encoder->compute_config_late)
			continue;

		ret = encoder->compute_config_late(encoder, crtc_state,
						   conn_state);
		if (ret)
			return ret;
	}

	return 0;
}

bool intel_fuzzy_clock_check(int clock1, int clock2)
{
	int diff;

	if (clock1 == clock2)
		return true;

	if (!clock1 || !clock2)
		return false;

	diff = abs(clock1 - clock2);

	if (((((diff + clock1 + clock2) * 100)) / (clock1 + clock2)) < 105)
		return true;

	return false;
}

static bool
intel_compare_link_m_n(const struct intel_link_m_n *m_n,
		       const struct intel_link_m_n *m2_n2)
{
	return m_n->tu == m2_n2->tu &&
		m_n->data_m == m2_n2->data_m &&
		m_n->data_n == m2_n2->data_n &&
		m_n->link_m == m2_n2->link_m &&
		m_n->link_n == m2_n2->link_n;
}

static bool
intel_compare_infoframe(const union hdmi_infoframe *a,
			const union hdmi_infoframe *b)
{
	return memcmp(a, b, sizeof(*a)) == 0;
}

static bool
intel_compare_dp_vsc_sdp(const struct drm_dp_vsc_sdp *a,
			 const struct drm_dp_vsc_sdp *b)
{
	return a->pixelformat == b->pixelformat &&
		a->colorimetry == b->colorimetry &&
		a->bpc == b->bpc &&
		a->dynamic_range == b->dynamic_range &&
		a->content_type == b->content_type;
}

static bool
intel_compare_dp_as_sdp(const struct drm_dp_as_sdp *a,
			const struct drm_dp_as_sdp *b)
{
	return a->vtotal == b->vtotal &&
		a->target_rr == b->target_rr &&
		a->duration_incr_ms == b->duration_incr_ms &&
		a->duration_decr_ms == b->duration_decr_ms &&
		a->mode == b->mode;
}

static bool
intel_compare_buffer(const u8 *a, const u8 *b, size_t len)
{
	return memcmp(a, b, len) == 0;
}

static void __printf(5, 6)
pipe_config_mismatch(struct drm_printer *p, bool fastset,
		     const struct intel_crtc *crtc,
		     const char *name, const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	if (fastset)
		drm_printf(p, "[CRTC:%d:%s] fastset requirement not met in %s %pV\n",
			   crtc->base.base.id, crtc->base.name, name, &vaf);
	else
		drm_printf(p, "[CRTC:%d:%s] mismatch in %s %pV\n",
			   crtc->base.base.id, crtc->base.name, name, &vaf);

	va_end(args);
}

static void
pipe_config_infoframe_mismatch(struct drm_printer *p, bool fastset,
			       const struct intel_crtc *crtc,
			       const char *name,
			       const union hdmi_infoframe *a,
			       const union hdmi_infoframe *b)
{
	struct intel_display *display = to_intel_display(crtc);
	const char *loglevel;

	if (fastset) {
		if (!drm_debug_enabled(DRM_UT_KMS))
			return;

		loglevel = KERN_DEBUG;
	} else {
		loglevel = KERN_ERR;
	}

	pipe_config_mismatch(p, fastset, crtc, name, "infoframe");

	drm_printf(p, "expected:\n");
	hdmi_infoframe_log(loglevel, display->drm->dev, a);
	drm_printf(p, "found:\n");
	hdmi_infoframe_log(loglevel, display->drm->dev, b);
}

static void
pipe_config_dp_vsc_sdp_mismatch(struct drm_printer *p, bool fastset,
				const struct intel_crtc *crtc,
				const char *name,
				const struct drm_dp_vsc_sdp *a,
				const struct drm_dp_vsc_sdp *b)
{
	pipe_config_mismatch(p, fastset, crtc, name, "dp vsc sdp");

	drm_printf(p, "expected:\n");
	drm_dp_vsc_sdp_log(p, a);
	drm_printf(p, "found:\n");
	drm_dp_vsc_sdp_log(p, b);
}

static void
pipe_config_dp_as_sdp_mismatch(struct drm_printer *p, bool fastset,
			       const struct intel_crtc *crtc,
			       const char *name,
			       const struct drm_dp_as_sdp *a,
			       const struct drm_dp_as_sdp *b)
{
	pipe_config_mismatch(p, fastset, crtc, name, "dp as sdp");

	drm_printf(p, "expected:\n");
	drm_dp_as_sdp_log(p, a);
	drm_printf(p, "found:\n");
	drm_dp_as_sdp_log(p, b);
}

/* Returns the length up to and including the last differing byte */
static size_t
memcmp_diff_len(const u8 *a, const u8 *b, size_t len)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (a[i] != b[i])
			return i + 1;
	}

	return 0;
}

static void
pipe_config_buffer_mismatch(struct drm_printer *p, bool fastset,
			    const struct intel_crtc *crtc,
			    const char *name,
			    const u8 *a, const u8 *b, size_t len)
{
	pipe_config_mismatch(p, fastset, crtc, name, "buffer");

	/* only dump up to the last difference */
	len = memcmp_diff_len(a, b, len);

	drm_print_hex_dump(p, "expected: ", a, len);
	drm_print_hex_dump(p, "found:    ", b, len);
}

static void
pipe_config_pll_mismatch(struct drm_printer *p, bool fastset,
			 const struct intel_crtc *crtc,
			 const char *name,
			 const struct intel_dpll_hw_state *a,
			 const struct intel_dpll_hw_state *b)
{
	struct intel_display *display = to_intel_display(crtc);

	pipe_config_mismatch(p, fastset, crtc, name, " "); /* stupid -Werror=format-zero-length */

	drm_printf(p, "expected:\n");
	intel_dpll_dump_hw_state(display, p, a);
	drm_printf(p, "found:\n");
	intel_dpll_dump_hw_state(display, p, b);
}

static void
pipe_config_cx0pll_mismatch(struct drm_printer *p, bool fastset,
			    const struct intel_crtc *crtc,
			    const char *name,
			    const struct intel_cx0pll_state *a,
			    const struct intel_cx0pll_state *b)
{
	struct intel_display *display = to_intel_display(crtc);
	char *chipname = a->use_c10 ? "C10" : "C20";

	pipe_config_mismatch(p, fastset, crtc, name, chipname);

	drm_printf(p, "expected:\n");
	intel_cx0pll_dump_hw_state(display, a);
	drm_printf(p, "found:\n");
	intel_cx0pll_dump_hw_state(display, b);
}

static bool allow_vblank_delay_fastset(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);

	/*
	 * Allow fastboot to fix up vblank delay (handled via LRR
	 * codepaths), a bit dodgy as the registers aren't
	 * double buffered but seems to be working more or less...
	 */
	return HAS_LRR(display) && old_crtc_state->inherited &&
		!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DSI);
}

bool
intel_pipe_config_compare(const struct intel_crtc_state *current_config,
			  const struct intel_crtc_state *pipe_config,
			  bool fastset)
{
	struct intel_display *display = to_intel_display(current_config);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_printer p;
	u32 exclude_infoframes = 0;
	bool ret = true;

	if (fastset)
		p = drm_dbg_printer(display->drm, DRM_UT_KMS, NULL);
	else
		p = drm_err_printer(display->drm, NULL);

#define PIPE_CONF_CHECK_X(name) do { \
	if (current_config->name != pipe_config->name) { \
		BUILD_BUG_ON_MSG(__same_type(current_config->name, bool), \
				 __stringify(name) " is bool");	\
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected 0x%08x, found 0x%08x)", \
				     current_config->name, \
				     pipe_config->name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_X_WITH_MASK(name, mask) do { \
	if ((current_config->name & (mask)) != (pipe_config->name & (mask))) { \
		BUILD_BUG_ON_MSG(__same_type(current_config->name, bool), \
				 __stringify(name) " is bool");	\
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected 0x%08x, found 0x%08x)", \
				     current_config->name & (mask), \
				     pipe_config->name & (mask)); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_I(name) do { \
	if (current_config->name != pipe_config->name) { \
		BUILD_BUG_ON_MSG(__same_type(current_config->name, bool), \
				 __stringify(name) " is bool");	\
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected %i, found %i)", \
				     current_config->name, \
				     pipe_config->name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_LLI(name) do { \
	if (current_config->name != pipe_config->name) { \
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected %lli, found %lli)", \
				     current_config->name, \
				     pipe_config->name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_BOOL(name) do { \
	if (current_config->name != pipe_config->name) { \
		BUILD_BUG_ON_MSG(!__same_type(current_config->name, bool), \
				 __stringify(name) " is not bool");	\
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected %s, found %s)", \
				     str_yes_no(current_config->name), \
				     str_yes_no(pipe_config->name)); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_P(name) do { \
	if (current_config->name != pipe_config->name) { \
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected %p, found %p)", \
				     current_config->name, \
				     pipe_config->name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_M_N(name) do { \
	if (!intel_compare_link_m_n(&current_config->name, \
				    &pipe_config->name)) { \
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(expected tu %i data %i/%i link %i/%i, " \
				     "found tu %i, data %i/%i link %i/%i)", \
				     current_config->name.tu, \
				     current_config->name.data_m, \
				     current_config->name.data_n, \
				     current_config->name.link_m, \
				     current_config->name.link_n, \
				     pipe_config->name.tu, \
				     pipe_config->name.data_m, \
				     pipe_config->name.data_n, \
				     pipe_config->name.link_m, \
				     pipe_config->name.link_n); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_PLL(name) do { \
	if (!intel_dpll_compare_hw_state(display, &current_config->name, \
					 &pipe_config->name)) { \
		pipe_config_pll_mismatch(&p, fastset, crtc, __stringify(name), \
					 &current_config->name, \
					 &pipe_config->name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_PLL_CX0(name) do { \
	if (!intel_cx0pll_compare_hw_state(&current_config->name, \
					   &pipe_config->name)) { \
		pipe_config_cx0pll_mismatch(&p, fastset, crtc, __stringify(name), \
					    &current_config->name, \
					    &pipe_config->name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_TIMINGS(name) do {     \
	PIPE_CONF_CHECK_I(name.crtc_hdisplay); \
	PIPE_CONF_CHECK_I(name.crtc_htotal); \
	PIPE_CONF_CHECK_I(name.crtc_hblank_start); \
	PIPE_CONF_CHECK_I(name.crtc_hblank_end); \
	PIPE_CONF_CHECK_I(name.crtc_hsync_start); \
	PIPE_CONF_CHECK_I(name.crtc_hsync_end); \
	PIPE_CONF_CHECK_I(name.crtc_vdisplay); \
	if (!fastset || !allow_vblank_delay_fastset(current_config)) \
		PIPE_CONF_CHECK_I(name.crtc_vblank_start); \
	PIPE_CONF_CHECK_I(name.crtc_vsync_start); \
	PIPE_CONF_CHECK_I(name.crtc_vsync_end); \
	if (!fastset || !pipe_config->update_lrr) { \
		PIPE_CONF_CHECK_I(name.crtc_vtotal); \
		PIPE_CONF_CHECK_I(name.crtc_vblank_end); \
	} \
} while (0)

#define PIPE_CONF_CHECK_RECT(name) do { \
	PIPE_CONF_CHECK_I(name.x1); \
	PIPE_CONF_CHECK_I(name.x2); \
	PIPE_CONF_CHECK_I(name.y1); \
	PIPE_CONF_CHECK_I(name.y2); \
} while (0)

#define PIPE_CONF_CHECK_FLAGS(name, mask) do { \
	if ((current_config->name ^ pipe_config->name) & (mask)) { \
		pipe_config_mismatch(&p, fastset, crtc, __stringify(name), \
				     "(%x) (expected %i, found %i)", \
				     (mask), \
				     current_config->name & (mask), \
				     pipe_config->name & (mask)); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_INFOFRAME(name) do { \
	if (!intel_compare_infoframe(&current_config->infoframes.name, \
				     &pipe_config->infoframes.name)) { \
		pipe_config_infoframe_mismatch(&p, fastset, crtc, __stringify(name), \
					       &current_config->infoframes.name, \
					       &pipe_config->infoframes.name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_DP_VSC_SDP(name) do { \
	if (!intel_compare_dp_vsc_sdp(&current_config->infoframes.name, \
				      &pipe_config->infoframes.name)) { \
		pipe_config_dp_vsc_sdp_mismatch(&p, fastset, crtc, __stringify(name), \
						&current_config->infoframes.name, \
						&pipe_config->infoframes.name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_DP_AS_SDP(name) do { \
	if (!intel_compare_dp_as_sdp(&current_config->infoframes.name, \
				      &pipe_config->infoframes.name)) { \
		pipe_config_dp_as_sdp_mismatch(&p, fastset, crtc, __stringify(name), \
						&current_config->infoframes.name, \
						&pipe_config->infoframes.name); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_BUFFER(name, len) do { \
	BUILD_BUG_ON(sizeof(current_config->name) != (len)); \
	BUILD_BUG_ON(sizeof(pipe_config->name) != (len)); \
	if (!intel_compare_buffer(current_config->name, pipe_config->name, (len))) { \
		pipe_config_buffer_mismatch(&p, fastset, crtc, __stringify(name), \
					    current_config->name, \
					    pipe_config->name, \
					    (len)); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_COLOR_LUT(lut, is_pre_csc_lut) do { \
	if (current_config->gamma_mode == pipe_config->gamma_mode && \
	    !intel_color_lut_equal(current_config, \
				   current_config->lut, pipe_config->lut, \
				   is_pre_csc_lut)) {	\
		pipe_config_mismatch(&p, fastset, crtc, __stringify(lut), \
				     "hw_state doesn't match sw_state"); \
		ret = false; \
	} \
} while (0)

#define PIPE_CONF_CHECK_CSC(name) do { \
	PIPE_CONF_CHECK_X(name.preoff[0]); \
	PIPE_CONF_CHECK_X(name.preoff[1]); \
	PIPE_CONF_CHECK_X(name.preoff[2]); \
	PIPE_CONF_CHECK_X(name.coeff[0]); \
	PIPE_CONF_CHECK_X(name.coeff[1]); \
	PIPE_CONF_CHECK_X(name.coeff[2]); \
	PIPE_CONF_CHECK_X(name.coeff[3]); \
	PIPE_CONF_CHECK_X(name.coeff[4]); \
	PIPE_CONF_CHECK_X(name.coeff[5]); \
	PIPE_CONF_CHECK_X(name.coeff[6]); \
	PIPE_CONF_CHECK_X(name.coeff[7]); \
	PIPE_CONF_CHECK_X(name.coeff[8]); \
	PIPE_CONF_CHECK_X(name.postoff[0]); \
	PIPE_CONF_CHECK_X(name.postoff[1]); \
	PIPE_CONF_CHECK_X(name.postoff[2]); \
} while (0)

#define PIPE_CONF_QUIRK(quirk) \
	((current_config->quirks | pipe_config->quirks) & (quirk))

	PIPE_CONF_CHECK_BOOL(hw.enable);
	PIPE_CONF_CHECK_BOOL(hw.active);

	PIPE_CONF_CHECK_I(cpu_transcoder);
	PIPE_CONF_CHECK_I(mst_master_transcoder);

	PIPE_CONF_CHECK_BOOL(has_pch_encoder);
	PIPE_CONF_CHECK_I(fdi_lanes);
	PIPE_CONF_CHECK_M_N(fdi_m_n);

	PIPE_CONF_CHECK_I(lane_count);
	PIPE_CONF_CHECK_X(lane_lat_optim_mask);

	PIPE_CONF_CHECK_I(min_hblank);

	if (HAS_DOUBLE_BUFFERED_M_N(display)) {
		if (!fastset || !pipe_config->update_m_n)
			PIPE_CONF_CHECK_M_N(dp_m_n);
	} else {
		PIPE_CONF_CHECK_M_N(dp_m_n);
		PIPE_CONF_CHECK_M_N(dp_m2_n2);
	}

	PIPE_CONF_CHECK_X(output_types);

	PIPE_CONF_CHECK_I(framestart_delay);
	PIPE_CONF_CHECK_I(msa_timing_delay);

	PIPE_CONF_CHECK_TIMINGS(hw.pipe_mode);
	PIPE_CONF_CHECK_TIMINGS(hw.adjusted_mode);

	PIPE_CONF_CHECK_I(pixel_multiplier);

	PIPE_CONF_CHECK_FLAGS(hw.adjusted_mode.flags,
			      DRM_MODE_FLAG_INTERLACE);

	if (!PIPE_CONF_QUIRK(PIPE_CONFIG_QUIRK_MODE_SYNC_FLAGS)) {
		PIPE_CONF_CHECK_FLAGS(hw.adjusted_mode.flags,
				      DRM_MODE_FLAG_PHSYNC);
		PIPE_CONF_CHECK_FLAGS(hw.adjusted_mode.flags,
				      DRM_MODE_FLAG_NHSYNC);
		PIPE_CONF_CHECK_FLAGS(hw.adjusted_mode.flags,
				      DRM_MODE_FLAG_PVSYNC);
		PIPE_CONF_CHECK_FLAGS(hw.adjusted_mode.flags,
				      DRM_MODE_FLAG_NVSYNC);
	}

	PIPE_CONF_CHECK_I(output_format);
	PIPE_CONF_CHECK_BOOL(has_hdmi_sink);
	if ((DISPLAY_VER(display) < 8 && !display->platform.haswell) ||
	    display->platform.valleyview || display->platform.cherryview)
		PIPE_CONF_CHECK_BOOL(limited_color_range);

	PIPE_CONF_CHECK_BOOL(hdmi_scrambling);
	PIPE_CONF_CHECK_BOOL(hdmi_high_tmds_clock_ratio);
	PIPE_CONF_CHECK_BOOL(has_infoframe);
	PIPE_CONF_CHECK_BOOL(enhanced_framing);
	PIPE_CONF_CHECK_BOOL(fec_enable);

	if (!fastset) {
		PIPE_CONF_CHECK_BOOL(has_audio);
		PIPE_CONF_CHECK_BUFFER(eld, MAX_ELD_BYTES);
	}

	PIPE_CONF_CHECK_X(gmch_pfit.control);
	/* pfit ratios are autocomputed by the hw on gen4+ */
	if (DISPLAY_VER(display) < 4)
		PIPE_CONF_CHECK_X(gmch_pfit.pgm_ratios);
	PIPE_CONF_CHECK_X(gmch_pfit.lvds_border_bits);

	/*
	 * Changing the EDP transcoder input mux
	 * (A_ONOFF vs. A_ON) requires a full modeset.
	 */
	PIPE_CONF_CHECK_BOOL(pch_pfit.force_thru);

	if (!fastset) {
		PIPE_CONF_CHECK_RECT(pipe_src);

		PIPE_CONF_CHECK_BOOL(pch_pfit.enabled);
		PIPE_CONF_CHECK_RECT(pch_pfit.dst);

		PIPE_CONF_CHECK_I(scaler_state.scaler_id);
		PIPE_CONF_CHECK_I(pixel_rate);

		PIPE_CONF_CHECK_X(gamma_mode);
		if (display->platform.cherryview)
			PIPE_CONF_CHECK_X(cgm_mode);
		else
			PIPE_CONF_CHECK_X(csc_mode);
		PIPE_CONF_CHECK_BOOL(gamma_enable);
		PIPE_CONF_CHECK_BOOL(csc_enable);
		PIPE_CONF_CHECK_BOOL(wgc_enable);

		PIPE_CONF_CHECK_I(linetime);
		PIPE_CONF_CHECK_I(ips_linetime);

		PIPE_CONF_CHECK_COLOR_LUT(pre_csc_lut, true);
		PIPE_CONF_CHECK_COLOR_LUT(post_csc_lut, false);

		PIPE_CONF_CHECK_CSC(csc);
		PIPE_CONF_CHECK_CSC(output_csc);
	}

	PIPE_CONF_CHECK_BOOL(double_wide);

	if (display->dpll.mgr)
		PIPE_CONF_CHECK_P(intel_dpll);

	/* FIXME convert everything over the dpll_mgr */
	if (display->dpll.mgr || HAS_GMCH(display))
		PIPE_CONF_CHECK_PLL(dpll_hw_state);

	/* FIXME convert MTL+ platforms over to dpll_mgr */
	if (DISPLAY_VER(display) >= 14)
		PIPE_CONF_CHECK_PLL_CX0(dpll_hw_state.cx0pll);

	PIPE_CONF_CHECK_X(dsi_pll.ctrl);
	PIPE_CONF_CHECK_X(dsi_pll.div);

	if (display->platform.g4x || DISPLAY_VER(display) >= 5)
		PIPE_CONF_CHECK_I(pipe_bpp);

	if (!fastset || !pipe_config->update_m_n) {
		PIPE_CONF_CHECK_I(hw.pipe_mode.crtc_clock);
		PIPE_CONF_CHECK_I(hw.adjusted_mode.crtc_clock);
	}
	PIPE_CONF_CHECK_I(port_clock);

	PIPE_CONF_CHECK_I(min_voltage_level);

	if (current_config->has_psr || pipe_config->has_psr)
		exclude_infoframes |= intel_hdmi_infoframe_enable(DP_SDP_VSC);

	if (current_config->vrr.enable || pipe_config->vrr.enable)
		exclude_infoframes |= intel_hdmi_infoframe_enable(DP_SDP_ADAPTIVE_SYNC);

	PIPE_CONF_CHECK_X_WITH_MASK(infoframes.enable, ~exclude_infoframes);
	PIPE_CONF_CHECK_X(infoframes.gcp);
	PIPE_CONF_CHECK_INFOFRAME(avi);
	PIPE_CONF_CHECK_INFOFRAME(spd);
	PIPE_CONF_CHECK_INFOFRAME(hdmi);
	if (!fastset) {
		PIPE_CONF_CHECK_INFOFRAME(drm);
		PIPE_CONF_CHECK_DP_AS_SDP(as_sdp);
	}
	PIPE_CONF_CHECK_DP_VSC_SDP(vsc);

	PIPE_CONF_CHECK_X(sync_mode_slaves_mask);
	PIPE_CONF_CHECK_I(master_transcoder);
	PIPE_CONF_CHECK_X(joiner_pipes);

	PIPE_CONF_CHECK_BOOL(dsc.config.block_pred_enable);
	PIPE_CONF_CHECK_BOOL(dsc.config.convert_rgb);
	PIPE_CONF_CHECK_BOOL(dsc.config.simple_422);
	PIPE_CONF_CHECK_BOOL(dsc.config.native_422);
	PIPE_CONF_CHECK_BOOL(dsc.config.native_420);
	PIPE_CONF_CHECK_BOOL(dsc.config.vbr_enable);
	PIPE_CONF_CHECK_I(dsc.config.line_buf_depth);
	PIPE_CONF_CHECK_I(dsc.config.bits_per_component);
	PIPE_CONF_CHECK_I(dsc.config.pic_width);
	PIPE_CONF_CHECK_I(dsc.config.pic_height);
	PIPE_CONF_CHECK_I(dsc.config.slice_width);
	PIPE_CONF_CHECK_I(dsc.config.slice_height);
	PIPE_CONF_CHECK_I(dsc.config.initial_dec_delay);
	PIPE_CONF_CHECK_I(dsc.config.initial_xmit_delay);
	PIPE_CONF_CHECK_I(dsc.config.scale_decrement_interval);
	PIPE_CONF_CHECK_I(dsc.config.scale_increment_interval);
	PIPE_CONF_CHECK_I(dsc.config.initial_scale_value);
	PIPE_CONF_CHECK_I(dsc.config.first_line_bpg_offset);
	PIPE_CONF_CHECK_I(dsc.config.flatness_min_qp);
	PIPE_CONF_CHECK_I(dsc.config.flatness_max_qp);
	PIPE_CONF_CHECK_I(dsc.config.slice_bpg_offset);
	PIPE_CONF_CHECK_I(dsc.config.nfl_bpg_offset);
	PIPE_CONF_CHECK_I(dsc.config.initial_offset);
	PIPE_CONF_CHECK_I(dsc.config.final_offset);
	PIPE_CONF_CHECK_I(dsc.config.rc_model_size);
	PIPE_CONF_CHECK_I(dsc.config.rc_quant_incr_limit0);
	PIPE_CONF_CHECK_I(dsc.config.rc_quant_incr_limit1);
	PIPE_CONF_CHECK_I(dsc.config.slice_chunk_size);
	PIPE_CONF_CHECK_I(dsc.config.second_line_bpg_offset);
	PIPE_CONF_CHECK_I(dsc.config.nsl_bpg_offset);

	PIPE_CONF_CHECK_BOOL(dsc.compression_enable);
	PIPE_CONF_CHECK_I(dsc.num_streams);
	PIPE_CONF_CHECK_I(dsc.compressed_bpp_x16);

	PIPE_CONF_CHECK_BOOL(splitter.enable);
	PIPE_CONF_CHECK_I(splitter.link_count);
	PIPE_CONF_CHECK_I(splitter.pixel_overlap);

	if (!fastset) {
		PIPE_CONF_CHECK_BOOL(vrr.enable);
		PIPE_CONF_CHECK_I(vrr.vmin);
		PIPE_CONF_CHECK_I(vrr.vmax);
		PIPE_CONF_CHECK_I(vrr.flipline);
		PIPE_CONF_CHECK_I(vrr.vsync_start);
		PIPE_CONF_CHECK_I(vrr.vsync_end);
		PIPE_CONF_CHECK_LLI(cmrr.cmrr_m);
		PIPE_CONF_CHECK_LLI(cmrr.cmrr_n);
		PIPE_CONF_CHECK_BOOL(cmrr.enable);
	}

	if (!fastset || intel_vrr_always_use_vrr_tg(display)) {
		PIPE_CONF_CHECK_I(vrr.pipeline_full);
		PIPE_CONF_CHECK_I(vrr.guardband);
	}

#undef PIPE_CONF_CHECK_X
#undef PIPE_CONF_CHECK_I
#undef PIPE_CONF_CHECK_LLI
#undef PIPE_CONF_CHECK_BOOL
#undef PIPE_CONF_CHECK_P
#undef PIPE_CONF_CHECK_FLAGS
#undef PIPE_CONF_CHECK_COLOR_LUT
#undef PIPE_CONF_CHECK_TIMINGS
#undef PIPE_CONF_CHECK_RECT
#undef PIPE_CONF_QUIRK

	return ret;
}

static void
intel_verify_planes(struct intel_atomic_state *state)
{
	struct intel_plane *plane;
	const struct intel_plane_state *plane_state;
	int i;

	for_each_new_intel_plane_in_state(state, plane,
					  plane_state, i)
		assert_plane(plane, plane_state->is_y_plane ||
			     plane_state->uapi.visible);
}

static int intel_modeset_pipe(struct intel_atomic_state *state,
			      struct intel_crtc_state *crtc_state,
			      const char *reason)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int ret;

	drm_dbg_kms(display->drm, "[CRTC:%d:%s] Full modeset due to %s\n",
		    crtc->base.base.id, crtc->base.name, reason);

	ret = drm_atomic_add_affected_connectors(&state->base,
						 &crtc->base);
	if (ret)
		return ret;

	ret = intel_dp_tunnel_atomic_add_state_for_crtc(state, crtc);
	if (ret)
		return ret;

	ret = intel_dp_mst_add_topology_state_for_crtc(state, crtc);
	if (ret)
		return ret;

	ret = intel_plane_add_affected(state, crtc);
	if (ret)
		return ret;

	crtc_state->uapi.mode_changed = true;

	return 0;
}

/**
 * intel_modeset_pipes_in_mask_early - force a full modeset on a set of pipes
 * @state: intel atomic state
 * @reason: the reason for the full modeset
 * @mask: mask of pipes to modeset
 *
 * Add pipes in @mask to @state and force a full modeset on the enabled ones
 * due to the description in @reason.
 * This function can be called only before new plane states are computed.
 *
 * Returns 0 in case of success, negative error code otherwise.
 */
int intel_modeset_pipes_in_mask_early(struct intel_atomic_state *state,
				      const char *reason, u8 mask)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, mask) {
		struct intel_crtc_state *crtc_state;
		int ret;

		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->hw.enable ||
		    intel_crtc_needs_modeset(crtc_state))
			continue;

		ret = intel_modeset_pipe(state, crtc_state, reason);
		if (ret)
			return ret;
	}

	return 0;
}

static void
intel_crtc_flag_modeset(struct intel_crtc_state *crtc_state)
{
	crtc_state->uapi.mode_changed = true;

	crtc_state->update_pipe = false;
	crtc_state->update_m_n = false;
	crtc_state->update_lrr = false;
}

/**
 * intel_modeset_all_pipes_late - force a full modeset on all pipes
 * @state: intel atomic state
 * @reason: the reason for the full modeset
 *
 * Add all pipes to @state and force a full modeset on the active ones due to
 * the description in @reason.
 * This function can be called only after new plane states are computed already.
 *
 * Returns 0 in case of success, negative error code otherwise.
 */
int intel_modeset_all_pipes_late(struct intel_atomic_state *state,
				 const char *reason)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state;
		int ret;

		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->hw.active ||
		    intel_crtc_needs_modeset(crtc_state))
			continue;

		ret = intel_modeset_pipe(state, crtc_state, reason);
		if (ret)
			return ret;

		intel_crtc_flag_modeset(crtc_state);

		crtc_state->update_planes |= crtc_state->active_planes;
		crtc_state->async_flip_planes = 0;
		crtc_state->do_async_flip = false;
	}

	return 0;
}

int intel_modeset_commit_pipes(struct intel_display *display,
			       u8 pipe_mask,
			       struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct intel_crtc *crtc;
	int ret;

	state = drm_atomic_state_alloc(display->drm);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	to_intel_atomic_state(state)->internal = true;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, pipe_mask) {
		struct intel_crtc_state *crtc_state =
			intel_atomic_get_crtc_state(state, crtc);

		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			goto out;
		}

		crtc_state->uapi.connectors_changed = true;
	}

	ret = drm_atomic_commit(state);
out:
	drm_atomic_state_put(state);

	return ret;
}

/*
 * This implements the workaround described in the "notes" section of the mode
 * set sequence documentation. When going from no pipes or single pipe to
 * multiple pipes, and planes are enabled after the pipe, we need to wait at
 * least 2 vblanks on the first pipe before enabling planes on the second pipe.
 */
static int hsw_mode_set_planes_workaround(struct intel_atomic_state *state)
{
	struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	struct intel_crtc_state *first_crtc_state = NULL;
	struct intel_crtc_state *other_crtc_state = NULL;
	enum pipe first_pipe = INVALID_PIPE, enabled_pipe = INVALID_PIPE;
	int i;

	/* look at all crtc's that are going to be enabled in during modeset */
	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		if (!crtc_state->hw.active ||
		    !intel_crtc_needs_modeset(crtc_state))
			continue;

		if (first_crtc_state) {
			other_crtc_state = crtc_state;
			break;
		} else {
			first_crtc_state = crtc_state;
			first_pipe = crtc->pipe;
		}
	}

	/* No workaround needed? */
	if (!first_crtc_state)
		return 0;

	/* w/a possibly needed, check how many crtc's are already enabled. */
	for_each_intel_crtc(state->base.dev, crtc) {
		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->hsw_workaround_pipe = INVALID_PIPE;

		if (!crtc_state->hw.active ||
		    intel_crtc_needs_modeset(crtc_state))
			continue;

		/* 2 or more enabled crtcs means no need for w/a */
		if (enabled_pipe != INVALID_PIPE)
			return 0;

		enabled_pipe = crtc->pipe;
	}

	if (enabled_pipe != INVALID_PIPE)
		first_crtc_state->hsw_workaround_pipe = enabled_pipe;
	else if (other_crtc_state)
		other_crtc_state->hsw_workaround_pipe = first_pipe;

	return 0;
}

u8 intel_calc_active_pipes(struct intel_atomic_state *state,
			   u8 active_pipes)
{
	const struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc_state->hw.active)
			active_pipes |= BIT(crtc->pipe);
		else
			active_pipes &= ~BIT(crtc->pipe);
	}

	return active_pipes;
}

static int intel_modeset_checks(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);

	state->modeset = true;

	if (display->platform.haswell)
		return hsw_mode_set_planes_workaround(state);

	return 0;
}

static bool lrr_params_changed(const struct drm_display_mode *old_adjusted_mode,
			       const struct drm_display_mode *new_adjusted_mode)
{
	return old_adjusted_mode->crtc_vblank_start != new_adjusted_mode->crtc_vblank_start ||
		old_adjusted_mode->crtc_vblank_end != new_adjusted_mode->crtc_vblank_end ||
		old_adjusted_mode->crtc_vtotal != new_adjusted_mode->crtc_vtotal;
}

static void intel_crtc_check_fastset(const struct intel_crtc_state *old_crtc_state,
				     struct intel_crtc_state *new_crtc_state)
{
	struct intel_display *display = to_intel_display(new_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);

	/* only allow LRR when the timings stay within the VRR range */
	if (old_crtc_state->vrr.in_range != new_crtc_state->vrr.in_range)
		new_crtc_state->update_lrr = false;

	if (!intel_pipe_config_compare(old_crtc_state, new_crtc_state, true)) {
		drm_dbg_kms(display->drm, "[CRTC:%d:%s] fastset requirement not met, forcing full modeset\n",
			    crtc->base.base.id, crtc->base.name);
	} else {
		if (allow_vblank_delay_fastset(old_crtc_state))
			new_crtc_state->update_lrr = true;
		new_crtc_state->uapi.mode_changed = false;
	}

	if (intel_compare_link_m_n(&old_crtc_state->dp_m_n,
				   &new_crtc_state->dp_m_n))
		new_crtc_state->update_m_n = false;

	if (!lrr_params_changed(&old_crtc_state->hw.adjusted_mode,
				&new_crtc_state->hw.adjusted_mode))
		new_crtc_state->update_lrr = false;

	if (intel_crtc_needs_modeset(new_crtc_state))
		intel_crtc_flag_modeset(new_crtc_state);
	else
		new_crtc_state->update_pipe = true;
}

static int intel_atomic_check_crtcs(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state __maybe_unused *crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		int ret;

		ret = intel_crtc_atomic_check(state, crtc);
		if (ret) {
			drm_dbg_atomic(display->drm,
				       "[CRTC:%d:%s] atomic driver check failed\n",
				       crtc->base.base.id, crtc->base.name);
			return ret;
		}
	}

	return 0;
}

static bool intel_cpu_transcoders_need_modeset(struct intel_atomic_state *state,
					       u8 transcoders)
{
	const struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->hw.enable &&
		    transcoders & BIT(new_crtc_state->cpu_transcoder) &&
		    intel_crtc_needs_modeset(new_crtc_state))
			return true;
	}

	return false;
}

static bool intel_pipes_need_modeset(struct intel_atomic_state *state,
				     u8 pipes)
{
	const struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->hw.enable &&
		    pipes & BIT(crtc->pipe) &&
		    intel_crtc_needs_modeset(new_crtc_state))
			return true;
	}

	return false;
}

static int intel_atomic_check_joiner(struct intel_atomic_state *state,
				     struct intel_crtc *primary_crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *primary_crtc_state =
		intel_atomic_get_new_crtc_state(state, primary_crtc);
	struct intel_crtc *secondary_crtc;

	if (!primary_crtc_state->joiner_pipes)
		return 0;

	/* sanity check */
	if (drm_WARN_ON(display->drm,
			primary_crtc->pipe != joiner_primary_pipe(primary_crtc_state)))
		return -EINVAL;

	if (primary_crtc_state->joiner_pipes & ~joiner_pipes(display)) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] Cannot act as joiner primary "
			    "(need 0x%x as pipes, only 0x%x possible)\n",
			    primary_crtc->base.base.id, primary_crtc->base.name,
			    primary_crtc_state->joiner_pipes, joiner_pipes(display));
		return -EINVAL;
	}

	for_each_intel_crtc_in_pipe_mask(display->drm, secondary_crtc,
					 intel_crtc_joiner_secondary_pipes(primary_crtc_state)) {
		struct intel_crtc_state *secondary_crtc_state;
		int ret;

		secondary_crtc_state = intel_atomic_get_crtc_state(&state->base, secondary_crtc);
		if (IS_ERR(secondary_crtc_state))
			return PTR_ERR(secondary_crtc_state);

		/* primary being enabled, secondary was already configured? */
		if (secondary_crtc_state->uapi.enable) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] secondary is enabled as normal CRTC, but "
				    "[CRTC:%d:%s] claiming this CRTC for joiner.\n",
				    secondary_crtc->base.base.id, secondary_crtc->base.name,
				    primary_crtc->base.base.id, primary_crtc->base.name);
			return -EINVAL;
		}

		/*
		 * The state copy logic assumes the primary crtc gets processed
		 * before the secondary crtc during the main compute_config loop.
		 * This works because the crtcs are created in pipe order,
		 * and the hardware requires primary pipe < secondary pipe as well.
		 * Should that change we need to rethink the logic.
		 */
		if (WARN_ON(drm_crtc_index(&primary_crtc->base) >
			    drm_crtc_index(&secondary_crtc->base)))
			return -EINVAL;

		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] Used as secondary for joiner primary [CRTC:%d:%s]\n",
			    secondary_crtc->base.base.id, secondary_crtc->base.name,
			    primary_crtc->base.base.id, primary_crtc->base.name);

		secondary_crtc_state->joiner_pipes =
			primary_crtc_state->joiner_pipes;

		ret = copy_joiner_crtc_state_modeset(state, secondary_crtc);
		if (ret)
			return ret;
	}

	return 0;
}

static void kill_joiner_secondaries(struct intel_atomic_state *state,
				    struct intel_crtc *primary_crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *primary_crtc_state =
		intel_atomic_get_new_crtc_state(state, primary_crtc);
	struct intel_crtc *secondary_crtc;

	for_each_intel_crtc_in_pipe_mask(display->drm, secondary_crtc,
					 intel_crtc_joiner_secondary_pipes(primary_crtc_state)) {
		struct intel_crtc_state *secondary_crtc_state =
			intel_atomic_get_new_crtc_state(state, secondary_crtc);

		secondary_crtc_state->joiner_pipes = 0;

		intel_crtc_copy_uapi_to_hw_state_modeset(state, secondary_crtc);
	}

	primary_crtc_state->joiner_pipes = 0;
}

/**
 * DOC: asynchronous flip implementation
 *
 * Asynchronous page flip is the implementation for the DRM_MODE_PAGE_FLIP_ASYNC
 * flag. Currently async flip is only supported via the drmModePageFlip IOCTL.
 * Correspondingly, support is currently added for primary plane only.
 *
 * Async flip can only change the plane surface address, so anything else
 * changing is rejected from the intel_async_flip_check_hw() function.
 * Once this check is cleared, flip done interrupt is enabled using
 * the intel_crtc_enable_flip_done() function.
 *
 * As soon as the surface address register is written, flip done interrupt is
 * generated and the requested events are sent to the userspace in the interrupt
 * handler itself. The timestamp and sequence sent during the flip done event
 * correspond to the last vblank and have no relation to the actual time when
 * the flip done event was sent.
 */
static int intel_async_flip_check_uapi(struct intel_atomic_state *state,
				       struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *old_plane_state;
	struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	int i;

	if (!new_crtc_state->uapi.async_flip)
		return 0;

	if (!new_crtc_state->uapi.active) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] not active\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	if (intel_crtc_needs_modeset(new_crtc_state)) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] modeset required\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	/*
	 * FIXME: joiner+async flip is busted currently.
	 * Remove this check once the issues are fixed.
	 */
	if (new_crtc_state->joiner_pipes) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] async flip disallowed with joiner\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	for_each_oldnew_intel_plane_in_state(state, plane, old_plane_state,
					     new_plane_state, i) {
		if (plane->pipe != crtc->pipe)
			continue;

		/*
		 * TODO: Async flip is only supported through the page flip IOCTL
		 * as of now. So support currently added for primary plane only.
		 * Support for other planes on platforms on which supports
		 * this(vlv/chv and icl+) should be added when async flip is
		 * enabled in the atomic IOCTL path.
		 */
		if (!plane->async_flip) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] async flip not supported\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (!old_plane_state->uapi.fb || !new_plane_state->uapi.fb) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] no old or new framebuffer\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}
	}

	return 0;
}

static int intel_async_flip_check_hw(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state, *new_crtc_state;
	const struct intel_plane_state *new_plane_state, *old_plane_state;
	struct intel_plane *plane;
	int i;

	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

	if (!new_crtc_state->uapi.async_flip)
		return 0;

	if (!new_crtc_state->hw.active) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] not active\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	if (intel_crtc_needs_modeset(new_crtc_state)) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] modeset required\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	if (old_crtc_state->active_planes != new_crtc_state->active_planes) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] Active planes cannot be in async flip\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	for_each_oldnew_intel_plane_in_state(state, plane, old_plane_state,
					     new_plane_state, i) {
		if (plane->pipe != crtc->pipe)
			continue;

		/*
		 * Only async flip capable planes should be in the state
		 * if we're really about to ask the hardware to perform
		 * an async flip. We should never get this far otherwise.
		 */
		if (drm_WARN_ON(display->drm,
				new_crtc_state->do_async_flip && !plane->async_flip))
			return -EINVAL;

		/*
		 * Only check async flip capable planes other planes
		 * may be involved in the initial commit due to
		 * the wm0/ddb optimization.
		 *
		 * TODO maybe should track which planes actually
		 * were requested to do the async flip...
		 */
		if (!plane->async_flip)
			continue;

		if (!intel_plane_can_async_flip(plane, new_plane_state->hw.fb->format->format,
						new_plane_state->hw.fb->modifier)) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] pixel format %p4cc / modifier 0x%llx does not support async flip\n",
				    plane->base.base.id, plane->base.name,
				    &new_plane_state->hw.fb->format->format,
				    new_plane_state->hw.fb->modifier);
			return -EINVAL;
		}

		/*
		 * We turn the first async flip request into a sync flip
		 * so that we can reconfigure the plane (eg. change modifier).
		 */
		if (!new_crtc_state->do_async_flip)
			continue;

		if (old_plane_state->view.color_plane[0].mapping_stride !=
		    new_plane_state->view.color_plane[0].mapping_stride) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Stride cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.fb->modifier !=
		    new_plane_state->hw.fb->modifier) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Modifier cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.fb->format !=
		    new_plane_state->hw.fb->format) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Pixel format cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.rotation !=
		    new_plane_state->hw.rotation) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Rotation cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (skl_plane_aux_dist(old_plane_state, 0) !=
		    skl_plane_aux_dist(new_plane_state, 0)) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] AUX_DIST cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (!drm_rect_equals(&old_plane_state->uapi.src, &new_plane_state->uapi.src) ||
		    !drm_rect_equals(&old_plane_state->uapi.dst, &new_plane_state->uapi.dst)) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Size/co-ordinates cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.alpha != new_plane_state->hw.alpha) {
			drm_dbg_kms(display->drm,
				    "[PLANES:%d:%s] Alpha value cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.pixel_blend_mode !=
		    new_plane_state->hw.pixel_blend_mode) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Pixel blend mode cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.color_encoding != new_plane_state->hw.color_encoding) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Color encoding cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		if (old_plane_state->hw.color_range != new_plane_state->hw.color_range) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Color range cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}

		/* plane decryption is allow to change only in synchronous flips */
		if (old_plane_state->decrypt != new_plane_state->decrypt) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] Decryption cannot be changed in async flip\n",
				    plane->base.base.id, plane->base.name);
			return -EINVAL;
		}
	}

	return 0;
}

static int intel_joiner_add_affected_crtcs(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_plane_state *plane_state;
	struct intel_crtc_state *crtc_state;
	struct intel_plane *plane;
	struct intel_crtc *crtc;
	u8 affected_pipes = 0;
	u8 modeset_pipes = 0;
	int i;

	/*
	 * Any plane which is in use by the joiner needs its crtc.
	 * Pull those in first as this will not have happened yet
	 * if the plane remains disabled according to uapi.
	 */
	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		crtc = to_intel_crtc(plane_state->hw.crtc);
		if (!crtc)
			continue;

		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	/* Now pull in all joined crtcs */
	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		affected_pipes |= crtc_state->joiner_pipes;
		if (intel_crtc_needs_modeset(crtc_state))
			modeset_pipes |= crtc_state->joiner_pipes;
	}

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, affected_pipes) {
		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, modeset_pipes) {
		int ret;

		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		crtc_state->uapi.mode_changed = true;

		ret = drm_atomic_add_affected_connectors(&state->base, &crtc->base);
		if (ret)
			return ret;

		ret = intel_plane_add_affected(state, crtc);
		if (ret)
			return ret;
	}

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		/* Kill old joiner link, we may re-establish afterwards */
		if (intel_crtc_needs_modeset(crtc_state) &&
		    intel_crtc_is_joiner_primary(crtc_state))
			kill_joiner_secondaries(state, crtc);
	}

	return 0;
}

static int intel_atomic_check_config(struct intel_atomic_state *state,
				     struct intel_link_bw_limits *limits,
				     enum pipe *failed_pipe)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int ret;
	int i;

	*failed_pipe = INVALID_PIPE;

	ret = intel_joiner_add_affected_crtcs(state);
	if (ret)
		return ret;

	ret = intel_fdi_add_affected_crtcs(state);
	if (ret)
		return ret;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!intel_crtc_needs_modeset(new_crtc_state)) {
			if (intel_crtc_is_joiner_secondary(new_crtc_state))
				copy_joiner_crtc_state_nomodeset(state, crtc);
			else
				intel_crtc_copy_uapi_to_hw_state_nomodeset(state, crtc);
			continue;
		}

		if (drm_WARN_ON(display->drm, intel_crtc_is_joiner_secondary(new_crtc_state)))
			continue;

		ret = intel_crtc_prepare_cleared_state(state, crtc);
		if (ret)
			goto fail;

		if (!new_crtc_state->hw.enable)
			continue;

		ret = intel_modeset_pipe_config(state, crtc, limits);
		if (ret)
			goto fail;
	}

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		if (drm_WARN_ON(display->drm, intel_crtc_is_joiner_secondary(new_crtc_state)))
			continue;

		if (!new_crtc_state->hw.enable)
			continue;

		ret = intel_modeset_pipe_config_late(state, crtc);
		if (ret)
			goto fail;
	}

fail:
	if (ret)
		*failed_pipe = crtc->pipe;

	return ret;
}

static int intel_atomic_check_config_and_link(struct intel_atomic_state *state)
{
	struct intel_link_bw_limits new_limits;
	struct intel_link_bw_limits old_limits;
	int ret;

	intel_link_bw_init_limits(state, &new_limits);
	old_limits = new_limits;

	while (true) {
		enum pipe failed_pipe;

		ret = intel_atomic_check_config(state, &new_limits,
						&failed_pipe);
		if (ret) {
			/*
			 * The bpp limit for a pipe is below the minimum it supports, set the
			 * limit to the minimum and recalculate the config.
			 */
			if (ret == -EINVAL &&
			    intel_link_bw_set_bpp_limit_for_pipe(state,
								 &old_limits,
								 &new_limits,
								 failed_pipe))
				continue;

			break;
		}

		old_limits = new_limits;

		ret = intel_link_bw_atomic_check(state, &new_limits);
		if (ret != -EAGAIN)
			break;
	}

	return ret;
}
/**
 * intel_atomic_check - validate state object
 * @dev: drm device
 * @_state: state to validate
 */
int intel_atomic_check(struct drm_device *dev,
		       struct drm_atomic_state *_state)
{
	struct intel_display *display = to_intel_display(dev);
	struct intel_atomic_state *state = to_intel_atomic_state(_state);
	struct intel_crtc_state *old_crtc_state, *new_crtc_state;
	struct intel_crtc *crtc;
	int ret, i;
	bool any_ms = false;

	if (!intel_display_driver_check_access(display))
		return -ENODEV;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		/*
		 * crtc's state no longer considered to be inherited
		 * after the first userspace/client initiated commit.
		 */
		if (!state->internal)
			new_crtc_state->inherited = false;

		if (new_crtc_state->inherited != old_crtc_state->inherited)
			new_crtc_state->uapi.mode_changed = true;

		if (new_crtc_state->uapi.scaling_filter !=
		    old_crtc_state->uapi.scaling_filter)
			new_crtc_state->uapi.mode_changed = true;
	}

	intel_vrr_check_modeset(state);

	ret = drm_atomic_helper_check_modeset(dev, &state->base);
	if (ret)
		goto fail;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		ret = intel_async_flip_check_uapi(state, crtc);
		if (ret)
			return ret;
	}

	ret = intel_atomic_check_config_and_link(state);
	if (ret)
		goto fail;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		if (intel_crtc_is_joiner_secondary(new_crtc_state)) {
			drm_WARN_ON(display->drm, new_crtc_state->uapi.enable);
			continue;
		}

		ret = intel_atomic_check_joiner(state, crtc);
		if (ret)
			goto fail;
	}

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		intel_joiner_adjust_pipe_src(new_crtc_state);

		intel_crtc_check_fastset(old_crtc_state, new_crtc_state);
	}

	/**
	 * Check if fastset is allowed by external dependencies like other
	 * pipes and transcoders.
	 *
	 * Right now it only forces a fullmodeset when the MST master
	 * transcoder did not changed but the pipe of the master transcoder
	 * needs a fullmodeset so all slaves also needs to do a fullmodeset or
	 * in case of port synced crtcs, if one of the synced crtcs
	 * needs a full modeset, all other synced crtcs should be
	 * forced a full modeset.
	 */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->hw.enable || intel_crtc_needs_modeset(new_crtc_state))
			continue;

		if (intel_dp_mst_crtc_needs_modeset(state, crtc))
			intel_crtc_flag_modeset(new_crtc_state);

		if (intel_dp_mst_is_slave_trans(new_crtc_state)) {
			enum transcoder master = new_crtc_state->mst_master_transcoder;

			if (intel_cpu_transcoders_need_modeset(state, BIT(master)))
				intel_crtc_flag_modeset(new_crtc_state);
		}

		if (is_trans_port_sync_mode(new_crtc_state)) {
			u8 trans = new_crtc_state->sync_mode_slaves_mask;

			if (new_crtc_state->master_transcoder != INVALID_TRANSCODER)
				trans |= BIT(new_crtc_state->master_transcoder);

			if (intel_cpu_transcoders_need_modeset(state, trans))
				intel_crtc_flag_modeset(new_crtc_state);
		}

		if (new_crtc_state->joiner_pipes) {
			if (intel_pipes_need_modeset(state, new_crtc_state->joiner_pipes))
				intel_crtc_flag_modeset(new_crtc_state);
		}
	}

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		any_ms = true;

		intel_dpll_release(state, crtc);
	}

	if (any_ms && !check_digital_port_conflicts(state)) {
		drm_dbg_kms(display->drm,
			    "rejecting conflicting digital port configuration\n");
		ret = -EINVAL;
		goto fail;
	}

	ret = intel_plane_atomic_check(state);
	if (ret)
		goto fail;

	ret = intel_compute_global_watermarks(state);
	if (ret)
		goto fail;

	ret = intel_bw_atomic_check(state, any_ms);
	if (ret)
		goto fail;

	ret = intel_cdclk_atomic_check(state, &any_ms);
	if (ret)
		goto fail;

	if (intel_any_crtc_needs_modeset(state))
		any_ms = true;

	if (any_ms) {
		ret = intel_modeset_checks(state);
		if (ret)
			goto fail;

		ret = intel_modeset_calc_cdclk(state);
		if (ret)
			return ret;
	}

	ret = intel_pmdemand_atomic_check(state);
	if (ret)
		goto fail;

	ret = intel_atomic_check_crtcs(state);
	if (ret)
		goto fail;

	ret = intel_fbc_atomic_check(state);
	if (ret)
		goto fail;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		intel_color_assert_luts(new_crtc_state);

		ret = intel_async_flip_check_hw(state, crtc);
		if (ret)
			goto fail;

		/* Either full modeset or fastset (or neither), never both */
		drm_WARN_ON(display->drm,
			    intel_crtc_needs_modeset(new_crtc_state) &&
			    intel_crtc_needs_fastset(new_crtc_state));

		if (!intel_crtc_needs_modeset(new_crtc_state) &&
		    !intel_crtc_needs_fastset(new_crtc_state))
			continue;

		intel_crtc_state_dump(new_crtc_state, state,
				      intel_crtc_needs_modeset(new_crtc_state) ?
				      "modeset" : "fastset");
	}

	return 0;

 fail:
	if (ret == -EDEADLK)
		return ret;

	/*
	 * FIXME would probably be nice to know which crtc specifically
	 * caused the failure, in cases where we can pinpoint it.
	 */
	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i)
		intel_crtc_state_dump(new_crtc_state, state, "failed");

	return ret;
}

static int intel_atomic_prepare_commit(struct intel_atomic_state *state)
{
	int ret;

	ret = drm_atomic_helper_prepare_planes(state->base.dev, &state->base);
	if (ret < 0)
		return ret;

	return 0;
}

void intel_crtc_arm_fifo_underrun(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc);

	if (DISPLAY_VER(display) != 2 || crtc_state->active_planes)
		intel_set_cpu_fifo_underrun_reporting(display, crtc->pipe, true);

	if (crtc_state->has_pch_encoder) {
		enum pipe pch_transcoder =
			intel_crtc_pch_transcoder(crtc);

		intel_set_pch_fifo_underrun_reporting(display, pch_transcoder, true);
	}
}

static void intel_pipe_fastset(const struct intel_crtc_state *old_crtc_state,
			       const struct intel_crtc_state *new_crtc_state)
{
	struct intel_display *display = to_intel_display(new_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);

	/*
	 * Update pipe size and adjust fitter if needed: the reason for this is
	 * that in compute_mode_changes we check the native mode (not the pfit
	 * mode) to see if we can flip rather than do a full mode set. In the
	 * fastboot case, we'll flip, but if we don't update the pipesrc and
	 * pfit state, we'll end up with a big fb scanned out into the wrong
	 * sized surface.
	 */
	intel_set_pipe_src_size(new_crtc_state);

	/* on skylake this is done by detaching scalers */
	if (DISPLAY_VER(display) >= 9) {
		if (new_crtc_state->pch_pfit.enabled)
			skl_pfit_enable(new_crtc_state);
	} else if (HAS_PCH_SPLIT(display)) {
		if (new_crtc_state->pch_pfit.enabled)
			ilk_pfit_enable(new_crtc_state);
		else if (old_crtc_state->pch_pfit.enabled)
			ilk_pfit_disable(old_crtc_state);
	}

	/*
	 * The register is supposedly single buffered so perhaps
	 * not 100% correct to do this here. But SKL+ calculate
	 * this based on the adjust pixel rate so pfit changes do
	 * affect it and so it must be updated for fastsets.
	 * HSW/BDW only really need this here for fastboot, after
	 * that the value should not change without a full modeset.
	 */
	if (DISPLAY_VER(display) >= 9 ||
	    display->platform.broadwell || display->platform.haswell)
		hsw_set_linetime_wm(new_crtc_state);

	if (new_crtc_state->update_m_n)
		intel_cpu_transcoder_set_m1_n1(crtc, new_crtc_state->cpu_transcoder,
					       &new_crtc_state->dp_m_n);

	if (new_crtc_state->update_lrr)
		intel_set_transcoder_timings_lrr(new_crtc_state);
}

static void commit_pipe_pre_planes(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	bool modeset = intel_crtc_needs_modeset(new_crtc_state);

	drm_WARN_ON(display->drm, new_crtc_state->use_dsb || new_crtc_state->use_flipq);

	/*
	 * During modesets pipe configuration was programmed as the
	 * CRTC was enabled.
	 */
	if (!modeset) {
		if (intel_crtc_needs_color_update(new_crtc_state))
			intel_color_commit_arm(NULL, new_crtc_state);

		if (DISPLAY_VER(display) >= 9 || display->platform.broadwell)
			bdw_set_pipe_misc(NULL, new_crtc_state);

		if (intel_crtc_needs_fastset(new_crtc_state))
			intel_pipe_fastset(old_crtc_state, new_crtc_state);
	}

	intel_psr2_program_trans_man_trk_ctl(NULL, new_crtc_state);

	intel_atomic_update_watermarks(state, crtc);
}

static void commit_pipe_post_planes(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	bool modeset = intel_crtc_needs_modeset(new_crtc_state);

	drm_WARN_ON(display->drm, new_crtc_state->use_dsb || new_crtc_state->use_flipq);

	/*
	 * Disable the scaler(s) after the plane(s) so that we don't
	 * get a catastrophic underrun even if the two operations
	 * end up happening in two different frames.
	 */
	if (DISPLAY_VER(display) >= 9 && !modeset)
		skl_detach_scalers(NULL, new_crtc_state);

	if (!modeset &&
	    intel_crtc_needs_color_update(new_crtc_state) &&
	    !intel_color_uses_dsb(new_crtc_state) &&
	    HAS_DOUBLE_BUFFERED_LUT(display))
		intel_color_load_luts(new_crtc_state);

	if (intel_crtc_vrr_enabling(state, crtc))
		intel_vrr_enable(new_crtc_state);
}

static void intel_enable_crtc(struct intel_atomic_state *state,
			      struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc *pipe_crtc;

	if (!intel_crtc_needs_modeset(new_crtc_state))
		return;

	for_each_intel_crtc_in_pipe_mask_reverse(display->drm, pipe_crtc,
						 intel_crtc_joined_pipe_mask(new_crtc_state)) {
		const struct intel_crtc_state *pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		/* VRR will be enable later, if required */
		intel_crtc_update_active_timings(pipe_crtc_state, false);
	}

	intel_psr_notify_pipe_change(state, crtc, true);

	display->funcs.display->crtc_enable(state, crtc);

	/* vblanks work again, re-enable pipe CRC. */
	intel_crtc_enable_pipe_crc(crtc);
}

static void intel_pre_update_crtc(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	bool modeset = intel_crtc_needs_modeset(new_crtc_state);

	if (old_crtc_state->inherited ||
	    intel_crtc_needs_modeset(new_crtc_state)) {
		if (HAS_DPT(display))
			intel_dpt_configure(crtc);
	}

	if (!modeset) {
		if (new_crtc_state->preload_luts &&
		    intel_crtc_needs_color_update(new_crtc_state))
			intel_color_load_luts(new_crtc_state);

		intel_pre_plane_update(state, crtc);

		if (intel_crtc_needs_fastset(new_crtc_state))
			intel_encoders_update_pipe(state, crtc);

		if (DISPLAY_VER(display) >= 11 &&
		    intel_crtc_needs_fastset(new_crtc_state))
			icl_set_pipe_chicken(new_crtc_state);

		if (vrr_params_changed(old_crtc_state, new_crtc_state) ||
		    cmrr_params_changed(old_crtc_state, new_crtc_state))
			intel_vrr_set_transcoder_timings(new_crtc_state);
	}

	intel_fbc_update(state, crtc);

	drm_WARN_ON(display->drm, !intel_display_power_is_enabled(display, POWER_DOMAIN_DC_OFF));

	if (!modeset &&
	    intel_crtc_needs_color_update(new_crtc_state) &&
	    !new_crtc_state->use_dsb && !new_crtc_state->use_flipq)
		intel_color_commit_noarm(NULL, new_crtc_state);

	if (!new_crtc_state->use_dsb && !new_crtc_state->use_flipq)
		intel_crtc_planes_update_noarm(NULL, state, crtc);
}

static void intel_update_crtc(struct intel_atomic_state *state,
			      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (new_crtc_state->use_flipq) {
		intel_flipq_enable(new_crtc_state);

		intel_crtc_prepare_vblank_event(new_crtc_state, &crtc->flipq_event);

		intel_flipq_add(crtc, INTEL_FLIPQ_PLANE_1, 0, INTEL_DSB_0,
				new_crtc_state->dsb_commit);
	} else if (new_crtc_state->use_dsb) {
		intel_crtc_prepare_vblank_event(new_crtc_state, &crtc->dsb_event);

		intel_dsb_commit(new_crtc_state->dsb_commit);
	} else {
		/* Perform vblank evasion around commit operation */
		intel_pipe_update_start(state, crtc);

		if (new_crtc_state->dsb_commit)
			intel_dsb_commit(new_crtc_state->dsb_commit);

		commit_pipe_pre_planes(state, crtc);

		intel_crtc_planes_update_arm(NULL, state, crtc);

		commit_pipe_post_planes(state, crtc);

		intel_pipe_update_end(state, crtc);
	}

	/*
	 * VRR/Seamless M/N update may need to update frame timings.
	 *
	 * FIXME Should be synchronized with the start of vblank somehow...
	 */
	if (intel_crtc_vrr_enabling(state, crtc) ||
	    new_crtc_state->update_m_n || new_crtc_state->update_lrr)
		intel_crtc_update_active_timings(new_crtc_state,
						 new_crtc_state->vrr.enable);

	/*
	 * We usually enable FIFO underrun interrupts as part of the
	 * CRTC enable sequence during modesets.  But when we inherit a
	 * valid pipe configuration from the BIOS we need to take care
	 * of enabling them on the CRTC's first fastset.
	 */
	if (intel_crtc_needs_fastset(new_crtc_state) &&
	    old_crtc_state->inherited)
		intel_crtc_arm_fifo_underrun(crtc, new_crtc_state);
}

static void intel_old_crtc_state_disables(struct intel_atomic_state *state,
					  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc *pipe_crtc;

	/*
	 * We need to disable pipe CRC before disabling the pipe,
	 * or we race against vblank off.
	 */
	for_each_intel_crtc_in_pipe_mask(display->drm, pipe_crtc,
					 intel_crtc_joined_pipe_mask(old_crtc_state))
		intel_crtc_disable_pipe_crc(pipe_crtc);

	intel_psr_notify_pipe_change(state, crtc, false);

	display->funcs.display->crtc_disable(state, crtc);

	for_each_intel_crtc_in_pipe_mask(display->drm, pipe_crtc,
					 intel_crtc_joined_pipe_mask(old_crtc_state)) {
		const struct intel_crtc_state *new_pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		pipe_crtc->active = false;
		intel_fbc_disable(pipe_crtc);

		if (!new_pipe_crtc_state->hw.active)
			intel_initial_watermarks(state, pipe_crtc);
	}
}

static void intel_commit_modeset_disables(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_crtc *crtc;
	u8 disable_pipes = 0;
	int i;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		/*
		 * Needs to be done even for pipes
		 * that weren't enabled previously.
		 */
		intel_pre_plane_update(state, crtc);

		if (!old_crtc_state->hw.active)
			continue;

		disable_pipes |= BIT(crtc->pipe);
	}

	for_each_old_intel_crtc_in_state(state, crtc, old_crtc_state, i) {
		if ((disable_pipes & BIT(crtc->pipe)) == 0)
			continue;

		intel_crtc_disable_planes(state, crtc);

		drm_vblank_work_flush_all(&crtc->base);
	}

	/* Only disable port sync and MST slaves */
	for_each_old_intel_crtc_in_state(state, crtc, old_crtc_state, i) {
		if ((disable_pipes & BIT(crtc->pipe)) == 0)
			continue;

		if (intel_crtc_is_joiner_secondary(old_crtc_state))
			continue;

		/* In case of Transcoder port Sync master slave CRTCs can be
		 * assigned in any order and we need to make sure that
		 * slave CRTCs are disabled first and then master CRTC since
		 * Slave vblanks are masked till Master Vblanks.
		 */
		if (!is_trans_port_sync_slave(old_crtc_state) &&
		    !intel_dp_mst_is_slave_trans(old_crtc_state))
			continue;

		intel_old_crtc_state_disables(state, crtc);

		disable_pipes &= ~intel_crtc_joined_pipe_mask(old_crtc_state);
	}

	/* Disable everything else left on */
	for_each_old_intel_crtc_in_state(state, crtc, old_crtc_state, i) {
		if ((disable_pipes & BIT(crtc->pipe)) == 0)
			continue;

		if (intel_crtc_is_joiner_secondary(old_crtc_state))
			continue;

		intel_old_crtc_state_disables(state, crtc);

		disable_pipes &= ~intel_crtc_joined_pipe_mask(old_crtc_state);
	}

	drm_WARN_ON(display->drm, disable_pipes);
}

static void intel_commit_modeset_enables(struct intel_atomic_state *state)
{
	struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->hw.active)
			continue;

		intel_enable_crtc(state, crtc);
		intel_pre_update_crtc(state, crtc);
	}

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->hw.active)
			continue;

		intel_update_crtc(state, crtc);
	}
}

static void skl_commit_modeset_enables(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;
	struct intel_crtc_state *old_crtc_state, *new_crtc_state;
	struct skl_ddb_entry entries[I915_MAX_PIPES] = {};
	u8 update_pipes = 0, modeset_pipes = 0;
	int i;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		enum pipe pipe = crtc->pipe;

		if (!new_crtc_state->hw.active)
			continue;

		/* ignore allocations for crtc's that have been turned off. */
		if (!intel_crtc_needs_modeset(new_crtc_state)) {
			entries[pipe] = old_crtc_state->wm.skl.ddb;
			update_pipes |= BIT(pipe);
		} else {
			modeset_pipes |= BIT(pipe);
		}
	}

	/*
	 * Whenever the number of active pipes changes, we need to make sure we
	 * update the pipes in the right order so that their ddb allocations
	 * never overlap with each other between CRTC updates. Otherwise we'll
	 * cause pipe underruns and other bad stuff.
	 *
	 * So first lets enable all pipes that do not need a fullmodeset as
	 * those don't have any external dependency.
	 */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		enum pipe pipe = crtc->pipe;

		if ((update_pipes & BIT(pipe)) == 0)
			continue;

		intel_pre_update_crtc(state, crtc);
	}

	intel_dbuf_mbus_pre_ddb_update(state);

	while (update_pipes) {
		/*
		 * Commit in reverse order to make joiner primary
		 * send the uapi events after secondaries are done.
		 */
		for_each_oldnew_intel_crtc_in_state_reverse(state, crtc, old_crtc_state,
							    new_crtc_state, i) {
			enum pipe pipe = crtc->pipe;

			if ((update_pipes & BIT(pipe)) == 0)
				continue;

			if (skl_ddb_allocation_overlaps(&new_crtc_state->wm.skl.ddb,
							entries, I915_MAX_PIPES, pipe))
				continue;

			entries[pipe] = new_crtc_state->wm.skl.ddb;
			update_pipes &= ~BIT(pipe);

			intel_update_crtc(state, crtc);

			/*
			 * If this is an already active pipe, it's DDB changed,
			 * and this isn't the last pipe that needs updating
			 * then we need to wait for a vblank to pass for the
			 * new ddb allocation to take effect.
			 */
			if (!skl_ddb_entry_equal(&new_crtc_state->wm.skl.ddb,
						 &old_crtc_state->wm.skl.ddb) &&
			    (update_pipes | modeset_pipes))
				intel_crtc_wait_for_next_vblank(crtc);
		}
	}

	intel_dbuf_mbus_post_ddb_update(state);

	update_pipes = modeset_pipes;

	/*
	 * Enable all pipes that needs a modeset and do not depends on other
	 * pipes
	 */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		enum pipe pipe = crtc->pipe;

		if ((modeset_pipes & BIT(pipe)) == 0)
			continue;

		if (intel_crtc_is_joiner_secondary(new_crtc_state))
			continue;

		if (intel_dp_mst_is_slave_trans(new_crtc_state) ||
		    is_trans_port_sync_master(new_crtc_state))
			continue;

		modeset_pipes &= ~intel_crtc_joined_pipe_mask(new_crtc_state);

		intel_enable_crtc(state, crtc);
	}

	/*
	 * Then we enable all remaining pipes that depend on other
	 * pipes: MST slaves and port sync masters
	 */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		enum pipe pipe = crtc->pipe;

		if ((modeset_pipes & BIT(pipe)) == 0)
			continue;

		if (intel_crtc_is_joiner_secondary(new_crtc_state))
			continue;

		modeset_pipes &= ~intel_crtc_joined_pipe_mask(new_crtc_state);

		intel_enable_crtc(state, crtc);
	}

	/*
	 * Finally we do the plane updates/etc. for all pipes that got enabled.
	 */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		enum pipe pipe = crtc->pipe;

		if ((update_pipes & BIT(pipe)) == 0)
			continue;

		intel_pre_update_crtc(state, crtc);
	}

	/*
	 * Commit in reverse order to make joiner primary
	 * send the uapi events after secondaries are done.
	 */
	for_each_new_intel_crtc_in_state_reverse(state, crtc, new_crtc_state, i) {
		enum pipe pipe = crtc->pipe;

		if ((update_pipes & BIT(pipe)) == 0)
			continue;

		drm_WARN_ON(display->drm,
			    skl_ddb_allocation_overlaps(&new_crtc_state->wm.skl.ddb,
							entries, I915_MAX_PIPES, pipe));

		entries[pipe] = new_crtc_state->wm.skl.ddb;
		update_pipes &= ~BIT(pipe);

		intel_update_crtc(state, crtc);
	}

	drm_WARN_ON(display->drm, modeset_pipes);
	drm_WARN_ON(display->drm, update_pipes);
}

static void intel_atomic_commit_fence_wait(struct intel_atomic_state *intel_state)
{
	struct drm_i915_private *i915 = to_i915(intel_state->base.dev);
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	long ret;
	int i;

	for_each_new_plane_in_state(&intel_state->base, plane, new_plane_state, i) {
		if (new_plane_state->fence) {
			ret = dma_fence_wait_timeout(new_plane_state->fence, false,
						     i915_fence_timeout(i915));
			if (ret <= 0)
				break;

			dma_fence_put(new_plane_state->fence);
			new_plane_state->fence = NULL;
		}
	}
}

static void intel_atomic_dsb_wait_commit(struct intel_crtc_state *crtc_state)
{
	if (crtc_state->dsb_commit)
		intel_dsb_wait(crtc_state->dsb_commit);

	intel_color_wait_commit(crtc_state);
}

static void intel_atomic_dsb_cleanup(struct intel_crtc_state *crtc_state)
{
	if (crtc_state->dsb_commit) {
		intel_dsb_cleanup(crtc_state->dsb_commit);
		crtc_state->dsb_commit = NULL;
	}

	intel_color_cleanup_commit(crtc_state);
}

static void intel_atomic_cleanup_work(struct work_struct *work)
{
	struct intel_atomic_state *state =
		container_of(work, struct intel_atomic_state, cleanup_work);
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *old_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_old_intel_crtc_in_state(state, crtc, old_crtc_state, i)
		intel_atomic_dsb_cleanup(old_crtc_state);

	drm_atomic_helper_cleanup_planes(display->drm, &state->base);
	drm_atomic_helper_commit_cleanup_done(&state->base);
	drm_atomic_state_put(&state->base);
}

static void intel_atomic_prepare_plane_clear_colors(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_plane *plane;
	struct intel_plane_state *plane_state;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct drm_framebuffer *fb = plane_state->hw.fb;
		int cc_plane;
		int ret;

		if (!fb)
			continue;

		cc_plane = intel_fb_rc_ccs_cc_plane(fb);
		if (cc_plane < 0)
			continue;

		/*
		 * The layout of the fast clear color value expected by HW
		 * (the DRM ABI requiring this value to be located in fb at
		 * offset 0 of cc plane, plane #2 previous generations or
		 * plane #1 for flat ccs):
		 * - 4 x 4 bytes per-channel value
		 *   (in surface type specific float/int format provided by the fb user)
		 * - 8 bytes native color value used by the display
		 *   (converted/written by GPU during a fast clear operation using the
		 *    above per-channel values)
		 *
		 * The commit's FB prepare hook already ensured that FB obj is pinned and the
		 * caller made sure that the object is synced wrt. the related color clear value
		 * GPU write on it.
		 */
		ret = intel_bo_read_from_page(intel_fb_bo(fb),
					      fb->offsets[cc_plane] + 16,
					      &plane_state->ccval,
					      sizeof(plane_state->ccval));
		/* The above could only fail if the FB obj has an unexpected backing store type. */
		drm_WARN_ON(display->drm, ret);
	}
}

static void intel_atomic_dsb_prepare(struct intel_atomic_state *state,
				     struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!new_crtc_state->hw.active)
		return;

	if (state->base.legacy_cursor_update)
		return;

	/* FIXME deal with everything */
	new_crtc_state->use_flipq =
		intel_flipq_supported(display) &&
		!new_crtc_state->do_async_flip &&
		!new_crtc_state->vrr.enable &&
		!new_crtc_state->has_psr &&
		!intel_crtc_needs_modeset(new_crtc_state) &&
		!intel_crtc_needs_fastset(new_crtc_state) &&
		!intel_crtc_needs_color_update(new_crtc_state);

	new_crtc_state->use_dsb =
		!new_crtc_state->use_flipq &&
		!new_crtc_state->do_async_flip &&
		(DISPLAY_VER(display) >= 20 || !new_crtc_state->has_psr) &&
		!intel_crtc_needs_modeset(new_crtc_state) &&
		!intel_crtc_needs_fastset(new_crtc_state);

	intel_color_prepare_commit(state, crtc);
}

static void intel_atomic_dsb_finish(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!new_crtc_state->use_flipq &&
	    !new_crtc_state->use_dsb &&
	    !new_crtc_state->dsb_color)
		return;

	/*
	 * Rough estimate:
	 * ~64 registers per each plane * 8 planes = 512
	 * Double that for pipe stuff and other overhead.
	 */
	new_crtc_state->dsb_commit = intel_dsb_prepare(state, crtc, INTEL_DSB_0,
						       new_crtc_state->use_dsb ||
						       new_crtc_state->use_flipq ? 1024 : 16);
	if (!new_crtc_state->dsb_commit) {
		new_crtc_state->use_flipq = false;
		new_crtc_state->use_dsb = false;
		intel_color_cleanup_commit(new_crtc_state);
		return;
	}

	if (new_crtc_state->use_flipq || new_crtc_state->use_dsb) {
		/* Wa_18034343758 */
		if (new_crtc_state->use_flipq)
			intel_flipq_wait_dmc_halt(new_crtc_state->dsb_commit, crtc);

		if (intel_crtc_needs_color_update(new_crtc_state))
			intel_color_commit_noarm(new_crtc_state->dsb_commit,
						 new_crtc_state);
		intel_crtc_planes_update_noarm(new_crtc_state->dsb_commit,
					       state, crtc);

		/*
		 * Ensure we have "Frame Change" event when PSR state is
		 * SRDENT(PSR1) or DEEP_SLEEP(PSR2). Otherwise DSB vblank
		 * evasion hangs as PIPEDSL is reading as 0.
		 */
		intel_psr_trigger_frame_change_event(new_crtc_state->dsb_commit,
						     state, crtc);

		if (new_crtc_state->use_dsb)
			intel_dsb_vblank_evade(state, new_crtc_state->dsb_commit);

		if (intel_crtc_needs_color_update(new_crtc_state))
			intel_color_commit_arm(new_crtc_state->dsb_commit,
					       new_crtc_state);
		bdw_set_pipe_misc(new_crtc_state->dsb_commit,
				  new_crtc_state);
		intel_psr2_program_trans_man_trk_ctl(new_crtc_state->dsb_commit,
						     new_crtc_state);
		intel_crtc_planes_update_arm(new_crtc_state->dsb_commit,
					     state, crtc);

		if (DISPLAY_VER(display) >= 9)
			skl_detach_scalers(new_crtc_state->dsb_commit,
					   new_crtc_state);

		/* Wa_18034343758 */
		if (new_crtc_state->use_flipq)
			intel_flipq_unhalt_dmc(new_crtc_state->dsb_commit, crtc);
	}

	if (intel_color_uses_chained_dsb(new_crtc_state))
		intel_dsb_chain(state, new_crtc_state->dsb_commit,
				new_crtc_state->dsb_color, true);
	else if (intel_color_uses_gosub_dsb(new_crtc_state))
		intel_dsb_gosub(new_crtc_state->dsb_commit,
				new_crtc_state->dsb_color);

	if (new_crtc_state->use_dsb && !intel_color_uses_chained_dsb(new_crtc_state)) {
		intel_dsb_wait_vblanks(new_crtc_state->dsb_commit, 1);

		intel_vrr_send_push(new_crtc_state->dsb_commit, new_crtc_state);
		intel_dsb_wait_vblank_delay(state, new_crtc_state->dsb_commit);
		intel_vrr_check_push_sent(new_crtc_state->dsb_commit,
					  new_crtc_state);
		intel_dsb_interrupt(new_crtc_state->dsb_commit);
	}

	intel_dsb_finish(new_crtc_state->dsb_commit);
}

static void intel_atomic_commit_tail(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private __maybe_unused *dev_priv = to_i915(display->drm);
	struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_crtc *crtc;
	struct intel_power_domain_mask put_domains[I915_MAX_PIPES] = {};
	intel_wakeref_t wakeref = NULL;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
		intel_atomic_dsb_prepare(state, crtc);

	intel_atomic_commit_fence_wait(state);

	intel_td_flush(display);

	intel_atomic_prepare_plane_clear_colors(state);

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
		intel_fbc_prepare_dirty_rect(state, crtc);

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
		intel_atomic_dsb_finish(state, crtc);

	drm_atomic_helper_wait_for_dependencies(&state->base);
	drm_dp_mst_atomic_wait_for_dependencies(&state->base);
	intel_atomic_global_state_wait_for_dependencies(state);

	/*
	 * During full modesets we write a lot of registers, wait
	 * for PLLs, etc. Doing that while DC states are enabled
	 * is not a good idea.
	 *
	 * During fastsets and other updates we also need to
	 * disable DC states due to the following scenario:
	 * 1. DC5 exit and PSR exit happen
	 * 2. Some or all _noarm() registers are written
	 * 3. Due to some long delay PSR is re-entered
	 * 4. DC5 entry -> DMC saves the already written new
	 *    _noarm() registers and the old not yet written
	 *    _arm() registers
	 * 5. DC5 exit -> DMC restores a mixture of old and
	 *    new register values and arms the update
	 * 6. PSR exit -> hardware latches a mixture of old and
	 *    new register values -> corrupted frame, or worse
	 * 7. New _arm() registers are finally written
	 * 8. Hardware finally latches a complete set of new
	 *    register values, and subsequent frames will be OK again
	 *
	 * Also note that due to the pipe CSC hardware issues on
	 * SKL/GLK DC states must remain off until the pipe CSC
	 * state readout has happened. Otherwise we risk corrupting
	 * the CSC latched register values with the readout (see
	 * skl_read_csc() and skl_color_commit_noarm()).
	 */
	wakeref = intel_display_power_get(display, POWER_DOMAIN_DC_OFF);

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		if (intel_crtc_needs_modeset(new_crtc_state) ||
		    intel_crtc_needs_fastset(new_crtc_state))
			intel_modeset_get_crtc_power_domains(new_crtc_state, &put_domains[crtc->pipe]);
	}

	intel_commit_modeset_disables(state);

	intel_dp_tunnel_atomic_alloc_bw(state);

	/* FIXME: Eventually get rid of our crtc->config pointer */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
		crtc->config = new_crtc_state;

	/*
	 * In XE_LPD+ Pmdemand combines many parameters such as voltage index,
	 * plls, cdclk frequency, QGV point selection parameter etc. Voltage
	 * index, cdclk/ddiclk frequencies are supposed to be configured before
	 * the cdclk config is set.
	 */
	intel_pmdemand_pre_plane_update(state);

	if (state->modeset) {
		drm_atomic_helper_update_legacy_modeset_state(display->drm, &state->base);

		intel_set_cdclk_pre_plane_update(state);

		intel_modeset_verify_disabled(state);
	}

	intel_sagv_pre_plane_update(state);

	/* Complete the events for pipes that have now been disabled */
	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		bool modeset = intel_crtc_needs_modeset(new_crtc_state);

		/* Complete events for now disable pipes here. */
		if (modeset && !new_crtc_state->hw.active && new_crtc_state->uapi.event) {
			spin_lock_irq(&display->drm->event_lock);
			drm_crtc_send_vblank_event(&crtc->base,
						   new_crtc_state->uapi.event);
			spin_unlock_irq(&display->drm->event_lock);

			new_crtc_state->uapi.event = NULL;
		}
	}

	intel_encoders_update_prepare(state);

	intel_dbuf_pre_plane_update(state);

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->do_async_flip)
			intel_crtc_enable_flip_done(state, crtc);
	}

	/* Now enable the clocks, plane, pipe, and connectors that we set up. */
	display->funcs.display->commit_modeset_enables(state);

	/* FIXME probably need to sequence this properly */
	intel_program_dpkgc_latency(state);

	intel_wait_for_vblank_workers(state);

	/* FIXME: We should call drm_atomic_helper_commit_hw_done() here
	 * already, but still need the state for the delayed optimization. To
	 * fix this:
	 * - wrap the optimization/post_plane_update stuff into a per-crtc work.
	 * - schedule that vblank worker _before_ calling hw_done
	 * - at the start of commit_tail, cancel it _synchrously
	 * - switch over to the vblank wait helper in the core after that since
	 *   we don't need out special handling any more.
	 */
	drm_atomic_helper_wait_for_flip_done(display->drm, &state->base);

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->do_async_flip)
			intel_crtc_disable_flip_done(state, crtc);

		intel_atomic_dsb_wait_commit(new_crtc_state);

		if (!state->base.legacy_cursor_update && !new_crtc_state->use_dsb)
			intel_vrr_check_push_sent(NULL, new_crtc_state);

		if (new_crtc_state->use_flipq)
			intel_flipq_disable(new_crtc_state);
	}

	/*
	 * Now that the vblank has passed, we can go ahead and program the
	 * optimal watermarks on platforms that need two-step watermark
	 * programming.
	 *
	 * TODO: Move this (and other cleanup) to an async worker eventually.
	 */
	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		/*
		 * Gen2 reports pipe underruns whenever all planes are disabled.
		 * So re-enable underrun reporting after some planes get enabled.
		 *
		 * We do this before .optimize_watermarks() so that we have a
		 * chance of catching underruns with the intermediate watermarks
		 * vs. the new plane configuration.
		 */
		if (DISPLAY_VER(display) == 2 && planes_enabling(old_crtc_state, new_crtc_state))
			intel_set_cpu_fifo_underrun_reporting(display, crtc->pipe, true);

		intel_optimize_watermarks(state, crtc);
	}

	intel_dbuf_post_plane_update(state);

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		intel_post_plane_update(state, crtc);

		intel_modeset_put_crtc_power_domains(crtc, &put_domains[crtc->pipe]);

		intel_modeset_verify_crtc(state, crtc);

		intel_post_plane_update_after_readout(state, crtc);

		/*
		 * DSB cleanup is done in cleanup_work aligning with framebuffer
		 * cleanup. So copy and reset the dsb structure to sync with
		 * commit_done and later do dsb cleanup in cleanup_work.
		 *
		 * FIXME get rid of this funny new->old swapping
		 */
		old_crtc_state->dsb_color = fetch_and_zero(&new_crtc_state->dsb_color);
		old_crtc_state->dsb_commit = fetch_and_zero(&new_crtc_state->dsb_commit);
	}

	/* Underruns don't always raise interrupts, so check manually */
	intel_check_cpu_fifo_underruns(display);
	intel_check_pch_fifo_underruns(display);

	if (state->modeset)
		intel_verify_planes(state);

	intel_sagv_post_plane_update(state);
	if (state->modeset)
		intel_set_cdclk_post_plane_update(state);
	intel_pmdemand_post_plane_update(state);

	drm_atomic_helper_commit_hw_done(&state->base);
	intel_atomic_global_state_commit_done(state);

	if (state->modeset) {
		/* As one of the primary mmio accessors, KMS has a high
		 * likelihood of triggering bugs in unclaimed access. After we
		 * finish modesetting, see if an error has been flagged, and if
		 * so enable debugging for the next modeset - and hope we catch
		 * the culprit.
		 */
		intel_uncore_arm_unclaimed_mmio_detection(&dev_priv->uncore);
	}
	/*
	 * Delay re-enabling DC states by 17 ms to avoid the off->on->off
	 * toggling overhead at and above 60 FPS.
	 */
	intel_display_power_put_async_delay(display, POWER_DOMAIN_DC_OFF, wakeref, 17);
	intel_display_rpm_put(display, state->wakeref);

	/*
	 * Defer the cleanup of the old state to a separate worker to not
	 * impede the current task (userspace for blocking modesets) that
	 * are executed inline. For out-of-line asynchronous modesets/flips,
	 * deferring to a new worker seems overkill, but we would place a
	 * schedule point (cond_resched()) here anyway to keep latencies
	 * down.
	 */
	INIT_WORK(&state->cleanup_work, intel_atomic_cleanup_work);
	queue_work(display->wq.cleanup, &state->cleanup_work);
}

static void intel_atomic_commit_work(struct work_struct *work)
{
	struct intel_atomic_state *state =
		container_of(work, struct intel_atomic_state, base.commit_work);

	intel_atomic_commit_tail(state);
}

static void intel_atomic_track_fbs(struct intel_atomic_state *state)
{
	struct intel_plane_state *old_plane_state, *new_plane_state;
	struct intel_plane *plane;
	int i;

	for_each_oldnew_intel_plane_in_state(state, plane, old_plane_state,
					     new_plane_state, i)
		intel_frontbuffer_track(to_intel_frontbuffer(old_plane_state->hw.fb),
					to_intel_frontbuffer(new_plane_state->hw.fb),
					plane->frontbuffer_bit);
}

static int intel_atomic_setup_commit(struct intel_atomic_state *state, bool nonblock)
{
	int ret;

	ret = drm_atomic_helper_setup_commit(&state->base, nonblock);
	if (ret)
		return ret;

	ret = intel_atomic_global_state_setup_commit(state);
	if (ret)
		return ret;

	return 0;
}

static int intel_atomic_swap_state(struct intel_atomic_state *state)
{
	int ret;

	ret = drm_atomic_helper_swap_state(&state->base, true);
	if (ret)
		return ret;

	intel_atomic_swap_global_state(state);

	intel_dpll_swap_state(state);

	intel_atomic_track_fbs(state);

	return 0;
}

int intel_atomic_commit(struct drm_device *dev, struct drm_atomic_state *_state,
			bool nonblock)
{
	struct intel_display *display = to_intel_display(dev);
	struct intel_atomic_state *state = to_intel_atomic_state(_state);
	int ret = 0;

	state->wakeref = intel_display_rpm_get(display);

	/*
	 * The intel_legacy_cursor_update() fast path takes care
	 * of avoiding the vblank waits for simple cursor
	 * movement and flips. For cursor on/off and size changes,
	 * we want to perform the vblank waits so that watermark
	 * updates happen during the correct frames. Gen9+ have
	 * double buffered watermarks and so shouldn't need this.
	 *
	 * Unset state->legacy_cursor_update before the call to
	 * drm_atomic_helper_setup_commit() because otherwise
	 * drm_atomic_helper_wait_for_flip_done() is a noop and
	 * we get FIFO underruns because we didn't wait
	 * for vblank.
	 *
	 * FIXME doing watermarks and fb cleanup from a vblank worker
	 * (assuming we had any) would solve these problems.
	 */
	if (DISPLAY_VER(display) < 9 && state->base.legacy_cursor_update) {
		struct intel_crtc_state *new_crtc_state;
		struct intel_crtc *crtc;
		int i;

		for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
			if (new_crtc_state->wm.need_postvbl_update ||
			    new_crtc_state->update_wm_post)
				state->base.legacy_cursor_update = false;
	}

	ret = intel_atomic_prepare_commit(state);
	if (ret) {
		drm_dbg_atomic(display->drm,
			       "Preparing state failed with %i\n", ret);
		intel_display_rpm_put(display, state->wakeref);
		return ret;
	}

	ret = intel_atomic_setup_commit(state, nonblock);
	if (!ret)
		ret = intel_atomic_swap_state(state);

	if (ret) {
		drm_atomic_helper_unprepare_planes(dev, &state->base);
		intel_display_rpm_put(display, state->wakeref);
		return ret;
	}

	drm_atomic_state_get(&state->base);
	INIT_WORK(&state->base.commit_work, intel_atomic_commit_work);

	if (nonblock && state->modeset) {
		queue_work(display->wq.modeset, &state->base.commit_work);
	} else if (nonblock) {
		queue_work(display->wq.flip, &state->base.commit_work);
	} else {
		if (state->modeset)
			flush_workqueue(display->wq.modeset);
		intel_atomic_commit_tail(state);
	}

	return 0;
}

static u32 intel_encoder_possible_clones(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_encoder *source_encoder;
	u32 possible_clones = 0;

	for_each_intel_encoder(display->drm, source_encoder) {
		if (encoders_cloneable(encoder, source_encoder))
			possible_clones |= drm_encoder_mask(&source_encoder->base);
	}

	return possible_clones;
}

static u32 intel_encoder_possible_crtcs(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc;
	u32 possible_crtcs = 0;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, encoder->pipe_mask)
		possible_crtcs |= drm_crtc_mask(&crtc->base);

	return possible_crtcs;
}

static bool ilk_has_edp_a(struct intel_display *display)
{
	if (!display->platform.mobile)
		return false;

	if ((intel_de_read(display, DP_A) & DP_DETECTED) == 0)
		return false;

	if (display->platform.ironlake && (intel_de_read(display, FUSE_STRAP) & ILK_eDP_A_DISABLE))
		return false;

	return true;
}

static bool intel_ddi_crt_present(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 9)
		return false;

	if (display->platform.haswell_ult || display->platform.broadwell_ult)
		return false;

	if (HAS_PCH_LPT_H(display) &&
	    intel_de_read(display, SFUSE_STRAP) & SFUSE_STRAP_CRT_DISABLED)
		return false;

	/* DDI E can't be used if DDI A requires 4 lanes */
	if (intel_de_read(display, DDI_BUF_CTL(PORT_A)) & DDI_A_4_LANES)
		return false;

	if (!display->vbt.int_crt_support)
		return false;

	return true;
}

bool assert_port_valid(struct intel_display *display, enum port port)
{
	return !drm_WARN(display->drm, !(DISPLAY_RUNTIME_INFO(display)->port_mask & BIT(port)),
			 "Platform does not support port %c\n", port_name(port));
}

void intel_setup_outputs(struct intel_display *display)
{
	struct intel_encoder *encoder;
	bool dpd_is_edp = false;

	intel_pps_unlock_regs_wa(display);

	if (!HAS_DISPLAY(display))
		return;

	if (HAS_DDI(display)) {
		if (intel_ddi_crt_present(display))
			intel_crt_init(display);

		intel_bios_for_each_encoder(display, intel_ddi_init);

		if (display->platform.geminilake || display->platform.broxton)
			vlv_dsi_init(display);
	} else if (HAS_PCH_SPLIT(display)) {
		int found;

		/*
		 * intel_edp_init_connector() depends on this completing first,
		 * to prevent the registration of both eDP and LVDS and the
		 * incorrect sharing of the PPS.
		 */
		intel_lvds_init(display);
		intel_crt_init(display);

		dpd_is_edp = intel_dp_is_port_edp(display, PORT_D);

		if (ilk_has_edp_a(display))
			g4x_dp_init(display, DP_A, PORT_A);

		if (intel_de_read(display, PCH_HDMIB) & SDVO_DETECTED) {
			/* PCH SDVOB multiplex with HDMIB */
			found = intel_sdvo_init(display, PCH_SDVOB, PORT_B);
			if (!found)
				g4x_hdmi_init(display, PCH_HDMIB, PORT_B);
			if (!found && (intel_de_read(display, PCH_DP_B) & DP_DETECTED))
				g4x_dp_init(display, PCH_DP_B, PORT_B);
		}

		if (intel_de_read(display, PCH_HDMIC) & SDVO_DETECTED)
			g4x_hdmi_init(display, PCH_HDMIC, PORT_C);

		if (!dpd_is_edp && intel_de_read(display, PCH_HDMID) & SDVO_DETECTED)
			g4x_hdmi_init(display, PCH_HDMID, PORT_D);

		if (intel_de_read(display, PCH_DP_C) & DP_DETECTED)
			g4x_dp_init(display, PCH_DP_C, PORT_C);

		if (intel_de_read(display, PCH_DP_D) & DP_DETECTED)
			g4x_dp_init(display, PCH_DP_D, PORT_D);
	} else if (display->platform.valleyview || display->platform.cherryview) {
		bool has_edp, has_port;

		if (display->platform.valleyview && display->vbt.int_crt_support)
			intel_crt_init(display);

		/*
		 * The DP_DETECTED bit is the latched state of the DDC
		 * SDA pin at boot. However since eDP doesn't require DDC
		 * (no way to plug in a DP->HDMI dongle) the DDC pins for
		 * eDP ports may have been muxed to an alternate function.
		 * Thus we can't rely on the DP_DETECTED bit alone to detect
		 * eDP ports. Consult the VBT as well as DP_DETECTED to
		 * detect eDP ports.
		 *
		 * Sadly the straps seem to be missing sometimes even for HDMI
		 * ports (eg. on Voyo V3 - CHT x7-Z8700), so check both strap
		 * and VBT for the presence of the port. Additionally we can't
		 * trust the port type the VBT declares as we've seen at least
		 * HDMI ports that the VBT claim are DP or eDP.
		 */
		has_edp = intel_dp_is_port_edp(display, PORT_B);
		has_port = intel_bios_is_port_present(display, PORT_B);
		if (intel_de_read(display, VLV_DP_B) & DP_DETECTED || has_port)
			has_edp &= g4x_dp_init(display, VLV_DP_B, PORT_B);
		if ((intel_de_read(display, VLV_HDMIB) & SDVO_DETECTED || has_port) && !has_edp)
			g4x_hdmi_init(display, VLV_HDMIB, PORT_B);

		has_edp = intel_dp_is_port_edp(display, PORT_C);
		has_port = intel_bios_is_port_present(display, PORT_C);
		if (intel_de_read(display, VLV_DP_C) & DP_DETECTED || has_port)
			has_edp &= g4x_dp_init(display, VLV_DP_C, PORT_C);
		if ((intel_de_read(display, VLV_HDMIC) & SDVO_DETECTED || has_port) && !has_edp)
			g4x_hdmi_init(display, VLV_HDMIC, PORT_C);

		if (display->platform.cherryview) {
			/*
			 * eDP not supported on port D,
			 * so no need to worry about it
			 */
			has_port = intel_bios_is_port_present(display, PORT_D);
			if (intel_de_read(display, CHV_DP_D) & DP_DETECTED || has_port)
				g4x_dp_init(display, CHV_DP_D, PORT_D);
			if (intel_de_read(display, CHV_HDMID) & SDVO_DETECTED || has_port)
				g4x_hdmi_init(display, CHV_HDMID, PORT_D);
		}

		vlv_dsi_init(display);
	} else if (display->platform.pineview) {
		intel_lvds_init(display);
		intel_crt_init(display);
	} else if (IS_DISPLAY_VER(display, 3, 4)) {
		bool found = false;

		if (display->platform.mobile)
			intel_lvds_init(display);

		intel_crt_init(display);

		if (intel_de_read(display, GEN3_SDVOB) & SDVO_DETECTED) {
			drm_dbg_kms(display->drm, "probing SDVOB\n");
			found = intel_sdvo_init(display, GEN3_SDVOB, PORT_B);
			if (!found && display->platform.g4x) {
				drm_dbg_kms(display->drm,
					    "probing HDMI on SDVOB\n");
				g4x_hdmi_init(display, GEN4_HDMIB, PORT_B);
			}

			if (!found && display->platform.g4x)
				g4x_dp_init(display, DP_B, PORT_B);
		}

		/* Before G4X SDVOC doesn't have its own detect register */

		if (intel_de_read(display, GEN3_SDVOB) & SDVO_DETECTED) {
			drm_dbg_kms(display->drm, "probing SDVOC\n");
			found = intel_sdvo_init(display, GEN3_SDVOC, PORT_C);
		}

		if (!found && (intel_de_read(display, GEN3_SDVOC) & SDVO_DETECTED)) {

			if (display->platform.g4x) {
				drm_dbg_kms(display->drm,
					    "probing HDMI on SDVOC\n");
				g4x_hdmi_init(display, GEN4_HDMIC, PORT_C);
			}
			if (display->platform.g4x)
				g4x_dp_init(display, DP_C, PORT_C);
		}

		if (display->platform.g4x && (intel_de_read(display, DP_D) & DP_DETECTED))
			g4x_dp_init(display, DP_D, PORT_D);

		if (SUPPORTS_TV(display))
			intel_tv_init(display);
	} else if (DISPLAY_VER(display) == 2) {
		if (display->platform.i85x)
			intel_lvds_init(display);

		intel_crt_init(display);
		intel_dvo_init(display);
	}

	for_each_intel_encoder(display->drm, encoder) {
		encoder->base.possible_crtcs =
			intel_encoder_possible_crtcs(encoder);
		encoder->base.possible_clones =
			intel_encoder_possible_clones(encoder);
	}

	intel_init_pch_refclk(display);

	drm_helper_move_panel_connectors_to_head(display->drm);
}

static int max_dotclock(struct intel_display *display)
{
	int max_dotclock = display->cdclk.max_dotclk_freq;

	if (HAS_ULTRAJOINER(display))
		max_dotclock *= 4;
	else if (HAS_UNCOMPRESSED_JOINER(display) || HAS_BIGJOINER(display))
		max_dotclock *= 2;

	return max_dotclock;
}

enum drm_mode_status intel_mode_valid(struct drm_device *dev,
				      const struct drm_display_mode *mode)
{
	struct intel_display *display = to_intel_display(dev);
	int hdisplay_max, htotal_max;
	int vdisplay_max, vtotal_max;

	/*
	 * Can't reject DBLSCAN here because Xorg ddxen can add piles
	 * of DBLSCAN modes to the output's mode list when they detect
	 * the scaling mode property on the connector. And they don't
	 * ask the kernel to validate those modes in any way until
	 * modeset time at which point the client gets a protocol error.
	 * So in order to not upset those clients we silently ignore the
	 * DBLSCAN flag on such connectors. For other connectors we will
	 * reject modes with the DBLSCAN flag in encoder->compute_config().
	 * And we always reject DBLSCAN modes in connector->mode_valid()
	 * as we never want such modes on the connector's mode list.
	 */

	if (mode->vscan > 1)
		return MODE_NO_VSCAN;

	if (mode->flags & DRM_MODE_FLAG_HSKEW)
		return MODE_H_ILLEGAL;

	if (mode->flags & (DRM_MODE_FLAG_CSYNC |
			   DRM_MODE_FLAG_NCSYNC |
			   DRM_MODE_FLAG_PCSYNC))
		return MODE_HSYNC;

	if (mode->flags & (DRM_MODE_FLAG_BCAST |
			   DRM_MODE_FLAG_PIXMUX |
			   DRM_MODE_FLAG_CLKDIV2))
		return MODE_BAD;

	/*
	 * Reject clearly excessive dotclocks early to
	 * avoid having to worry about huge integers later.
	 */
	if (mode->clock > max_dotclock(display))
		return MODE_CLOCK_HIGH;

	/* Transcoder timing limits */
	if (DISPLAY_VER(display) >= 11) {
		hdisplay_max = 16384;
		vdisplay_max = 8192;
		htotal_max = 16384;
		vtotal_max = 8192;
	} else if (DISPLAY_VER(display) >= 9 ||
		   display->platform.broadwell || display->platform.haswell) {
		hdisplay_max = 8192; /* FDI max 4096 handled elsewhere */
		vdisplay_max = 4096;
		htotal_max = 8192;
		vtotal_max = 8192;
	} else if (DISPLAY_VER(display) >= 3) {
		hdisplay_max = 4096;
		vdisplay_max = 4096;
		htotal_max = 8192;
		vtotal_max = 8192;
	} else {
		hdisplay_max = 2048;
		vdisplay_max = 2048;
		htotal_max = 4096;
		vtotal_max = 4096;
	}

	if (mode->hdisplay > hdisplay_max ||
	    mode->hsync_start > htotal_max ||
	    mode->hsync_end > htotal_max ||
	    mode->htotal > htotal_max)
		return MODE_H_ILLEGAL;

	if (mode->vdisplay > vdisplay_max ||
	    mode->vsync_start > vtotal_max ||
	    mode->vsync_end > vtotal_max ||
	    mode->vtotal > vtotal_max)
		return MODE_V_ILLEGAL;

	return MODE_OK;
}

enum drm_mode_status intel_cpu_transcoder_mode_valid(struct intel_display *display,
						     const struct drm_display_mode *mode)
{
	/*
	 * Additional transcoder timing limits,
	 * excluding BXT/GLK DSI transcoders.
	 */
	if (DISPLAY_VER(display) >= 5) {
		if (mode->hdisplay < 64 ||
		    mode->htotal - mode->hdisplay < 32)
			return MODE_H_ILLEGAL;

		if (mode->vtotal - mode->vdisplay < 5)
			return MODE_V_ILLEGAL;
	} else {
		if (mode->htotal - mode->hdisplay < 32)
			return MODE_H_ILLEGAL;

		if (mode->vtotal - mode->vdisplay < 3)
			return MODE_V_ILLEGAL;
	}

	/*
	 * Cantiga+ cannot handle modes with a hsync front porch of 0.
	 * WaPruneModeWithIncorrectHsyncOffset:ctg,elk,ilk,snb,ivb,vlv,hsw.
	 */
	if ((DISPLAY_VER(display) >= 5 || display->platform.g4x) &&
	    mode->hsync_start == mode->hdisplay)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

enum drm_mode_status
intel_mode_valid_max_plane_size(struct intel_display *display,
				const struct drm_display_mode *mode,
				int num_joined_pipes)
{
	int plane_width_max, plane_height_max;

	/*
	 * intel_mode_valid() should be
	 * sufficient on older platforms.
	 */
	if (DISPLAY_VER(display) < 9)
		return MODE_OK;

	/*
	 * Most people will probably want a fullscreen
	 * plane so let's not advertize modes that are
	 * too big for that.
	 */
	if (DISPLAY_VER(display) >= 30) {
		plane_width_max = 6144 * num_joined_pipes;
		plane_height_max = 4800;
	} else if (DISPLAY_VER(display) >= 11) {
		plane_width_max = 5120 * num_joined_pipes;
		plane_height_max = 4320;
	} else {
		plane_width_max = 5120;
		plane_height_max = 4096;
	}

	if (mode->hdisplay > plane_width_max)
		return MODE_H_ILLEGAL;

	if (mode->vdisplay > plane_height_max)
		return MODE_V_ILLEGAL;

	return MODE_OK;
}

static const struct intel_display_funcs skl_display_funcs = {
	.get_pipe_config = hsw_get_pipe_config,
	.crtc_enable = hsw_crtc_enable,
	.crtc_disable = hsw_crtc_disable,
	.commit_modeset_enables = skl_commit_modeset_enables,
	.get_initial_plane_config = skl_get_initial_plane_config,
	.fixup_initial_plane_config = skl_fixup_initial_plane_config,
};

static const struct intel_display_funcs ddi_display_funcs = {
	.get_pipe_config = hsw_get_pipe_config,
	.crtc_enable = hsw_crtc_enable,
	.crtc_disable = hsw_crtc_disable,
	.commit_modeset_enables = intel_commit_modeset_enables,
	.get_initial_plane_config = i9xx_get_initial_plane_config,
	.fixup_initial_plane_config = i9xx_fixup_initial_plane_config,
};

static const struct intel_display_funcs pch_split_display_funcs = {
	.get_pipe_config = ilk_get_pipe_config,
	.crtc_enable = ilk_crtc_enable,
	.crtc_disable = ilk_crtc_disable,
	.commit_modeset_enables = intel_commit_modeset_enables,
	.get_initial_plane_config = i9xx_get_initial_plane_config,
	.fixup_initial_plane_config = i9xx_fixup_initial_plane_config,
};

static const struct intel_display_funcs vlv_display_funcs = {
	.get_pipe_config = i9xx_get_pipe_config,
	.crtc_enable = valleyview_crtc_enable,
	.crtc_disable = i9xx_crtc_disable,
	.commit_modeset_enables = intel_commit_modeset_enables,
	.get_initial_plane_config = i9xx_get_initial_plane_config,
	.fixup_initial_plane_config = i9xx_fixup_initial_plane_config,
};

static const struct intel_display_funcs i9xx_display_funcs = {
	.get_pipe_config = i9xx_get_pipe_config,
	.crtc_enable = i9xx_crtc_enable,
	.crtc_disable = i9xx_crtc_disable,
	.commit_modeset_enables = intel_commit_modeset_enables,
	.get_initial_plane_config = i9xx_get_initial_plane_config,
	.fixup_initial_plane_config = i9xx_fixup_initial_plane_config,
};

/**
 * intel_init_display_hooks - initialize the display modesetting hooks
 * @display: display device private
 */
void intel_init_display_hooks(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 9) {
		display->funcs.display = &skl_display_funcs;
	} else if (HAS_DDI(display)) {
		display->funcs.display = &ddi_display_funcs;
	} else if (HAS_PCH_SPLIT(display)) {
		display->funcs.display = &pch_split_display_funcs;
	} else if (display->platform.cherryview ||
		   display->platform.valleyview) {
		display->funcs.display = &vlv_display_funcs;
	} else {
		display->funcs.display = &i9xx_display_funcs;
	}
}

int intel_initial_commit(struct intel_display *display)
{
	struct drm_atomic_state *state = NULL;
	struct drm_modeset_acquire_ctx ctx;
	struct intel_crtc *crtc;
	int ret = 0;

	state = drm_atomic_state_alloc(display->drm);
	if (!state)
		return -ENOMEM;

	drm_modeset_acquire_init(&ctx, 0);

	state->acquire_ctx = &ctx;
	to_intel_atomic_state(state)->internal = true;

retry:
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			intel_atomic_get_crtc_state(state, crtc);

		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			goto out;
		}

		if (!crtc_state->hw.active)
			crtc_state->inherited = false;

		if (crtc_state->hw.active) {
			struct intel_encoder *encoder;

			ret = drm_atomic_add_affected_planes(state, &crtc->base);
			if (ret)
				goto out;

			/*
			 * FIXME hack to force a LUT update to avoid the
			 * plane update forcing the pipe gamma on without
			 * having a proper LUT loaded. Remove once we
			 * have readout for pipe gamma enable.
			 */
			crtc_state->uapi.color_mgmt_changed = true;

			for_each_intel_encoder_mask(display->drm, encoder,
						    crtc_state->uapi.encoder_mask) {
				if (encoder->initial_fastset_check &&
				    !encoder->initial_fastset_check(encoder, crtc_state)) {
					ret = drm_atomic_add_affected_connectors(state,
										 &crtc->base);
					if (ret)
						goto out;
				}
			}
		}
	}

	ret = drm_atomic_commit(state);

out:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

void i830_enable_pipe(struct intel_display *display, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
	enum transcoder cpu_transcoder = (enum transcoder)pipe;
	/* 640x480@60Hz, ~25175 kHz */
	struct dpll clock = {
		.m1 = 18,
		.m2 = 7,
		.p1 = 13,
		.p2 = 4,
		.n = 2,
	};
	u32 dpll, fp;
	int i;

	drm_WARN_ON(display->drm,
		    i9xx_calc_dpll_params(48000, &clock) != 25154);

	drm_dbg_kms(display->drm,
		    "enabling pipe %c due to force quirk (vco=%d dot=%d)\n",
		    pipe_name(pipe), clock.vco, clock.dot);

	fp = i9xx_dpll_compute_fp(&clock);
	dpll = DPLL_DVO_2X_MODE |
		DPLL_VGA_MODE_DIS |
		((clock.p1 - 2) << DPLL_FPA01_P1_POST_DIV_SHIFT) |
		PLL_P2_DIVIDE_BY_4 |
		PLL_REF_INPUT_DREFCLK |
		DPLL_VCO_ENABLE;

	intel_de_write(display, TRANS_HTOTAL(display, cpu_transcoder),
		       HACTIVE(640 - 1) | HTOTAL(800 - 1));
	intel_de_write(display, TRANS_HBLANK(display, cpu_transcoder),
		       HBLANK_START(640 - 1) | HBLANK_END(800 - 1));
	intel_de_write(display, TRANS_HSYNC(display, cpu_transcoder),
		       HSYNC_START(656 - 1) | HSYNC_END(752 - 1));
	intel_de_write(display, TRANS_VTOTAL(display, cpu_transcoder),
		       VACTIVE(480 - 1) | VTOTAL(525 - 1));
	intel_de_write(display, TRANS_VBLANK(display, cpu_transcoder),
		       VBLANK_START(480 - 1) | VBLANK_END(525 - 1));
	intel_de_write(display, TRANS_VSYNC(display, cpu_transcoder),
		       VSYNC_START(490 - 1) | VSYNC_END(492 - 1));
	intel_de_write(display, PIPESRC(display, pipe),
		       PIPESRC_WIDTH(640 - 1) | PIPESRC_HEIGHT(480 - 1));

	intel_de_write(display, FP0(pipe), fp);
	intel_de_write(display, FP1(pipe), fp);

	/*
	 * Apparently we need to have VGA mode enabled prior to changing
	 * the P1/P2 dividers. Otherwise the DPLL will keep using the old
	 * dividers, even though the register value does change.
	 */
	intel_de_write(display, DPLL(display, pipe),
		       dpll & ~DPLL_VGA_MODE_DIS);
	intel_de_write(display, DPLL(display, pipe), dpll);

	/* Wait for the clocks to stabilize. */
	intel_de_posting_read(display, DPLL(display, pipe));
	udelay(150);

	/* The pixel multiplier can only be updated once the
	 * DPLL is enabled and the clocks are stable.
	 *
	 * So write it again.
	 */
	intel_de_write(display, DPLL(display, pipe), dpll);

	/* We do this three times for luck */
	for (i = 0; i < 3 ; i++) {
		intel_de_write(display, DPLL(display, pipe), dpll);
		intel_de_posting_read(display, DPLL(display, pipe));
		udelay(150); /* wait for warmup */
	}

	intel_de_write(display, TRANSCONF(display, pipe), TRANSCONF_ENABLE);
	intel_de_posting_read(display, TRANSCONF(display, pipe));

	intel_wait_for_pipe_scanline_moving(crtc);
}

void i830_disable_pipe(struct intel_display *display, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);

	drm_dbg_kms(display->drm, "disabling pipe %c due to force quirk\n",
		    pipe_name(pipe));

	drm_WARN_ON(display->drm,
		    intel_de_read(display, DSPCNTR(display, PLANE_A)) & DISP_ENABLE);
	drm_WARN_ON(display->drm,
		    intel_de_read(display, DSPCNTR(display, PLANE_B)) & DISP_ENABLE);
	drm_WARN_ON(display->drm,
		    intel_de_read(display, DSPCNTR(display, PLANE_C)) & DISP_ENABLE);
	drm_WARN_ON(display->drm,
		    intel_de_read(display, CURCNTR(display, PIPE_A)) & MCURSOR_MODE_MASK);
	drm_WARN_ON(display->drm,
		    intel_de_read(display, CURCNTR(display, PIPE_B)) & MCURSOR_MODE_MASK);

	intel_de_write(display, TRANSCONF(display, pipe), 0);
	intel_de_posting_read(display, TRANSCONF(display, pipe));

	intel_wait_for_pipe_scanline_stopped(crtc);

	intel_de_write(display, DPLL(display, pipe), DPLL_VGA_MODE_DIS);
	intel_de_posting_read(display, DPLL(display, pipe));
}

bool intel_scanout_needs_vtd_wa(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	return IS_DISPLAY_VER(display, 6, 11) && i915_vtd_active(i915);
}
