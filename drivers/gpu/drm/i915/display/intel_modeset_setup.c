// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 *
 * Read out the current hardware modeset state, and sanitize it to the current
 * state.
 */

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "i915_reg.h"
#include "i9xx_wm.h"
#include "intel_atomic.h"
#include "intel_bw.h"
#include "intel_cmtg.h"
#include "intel_color.h"
#include "intel_crtc.h"
#include "intel_crtc_state_dump.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_power.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_fifo_underrun.h"
#include "intel_modeset_setup.h"
#include "intel_pch_display.h"
#include "intel_pmdemand.h"
#include "intel_tc.h"
#include "intel_vblank.h"
#include "intel_vga.h"
#include "intel_wm.h"
#include "skl_watermark.h"

static void intel_crtc_disable_noatomic_begin(struct intel_crtc *crtc,
					      struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_plane *plane;
	struct drm_atomic_state *state;
	struct intel_crtc *temp_crtc;
	enum pipe pipe = crtc->pipe;

	if (!crtc_state->hw.active)
		return;

	for_each_intel_plane_on_crtc(display->drm, crtc, plane) {
		const struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (plane_state->uapi.visible)
			intel_plane_disable_noatomic(crtc, plane);
	}

	state = drm_atomic_state_alloc(display->drm);
	if (!state) {
		drm_dbg_kms(display->drm,
			    "failed to disable [CRTC:%d:%s], out of memory",
			    crtc->base.base.id, crtc->base.name);
		return;
	}

	state->acquire_ctx = ctx;
	to_intel_atomic_state(state)->internal = true;

	/* Everything's already locked, -EDEADLK can't happen. */
	for_each_intel_crtc_in_pipe_mask(display->drm, temp_crtc,
					 BIT(pipe) |
					 intel_crtc_joiner_secondary_pipes(crtc_state)) {
		struct intel_crtc_state *temp_crtc_state =
			intel_atomic_get_crtc_state(state, temp_crtc);
		int ret;

		ret = drm_atomic_add_affected_connectors(state, &temp_crtc->base);

		drm_WARN_ON(display->drm, IS_ERR(temp_crtc_state) || ret);
	}

	display->funcs.display->crtc_disable(to_intel_atomic_state(state), crtc);

	drm_atomic_state_put(state);

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] hw state adjusted, was enabled, now disabled\n",
		    crtc->base.base.id, crtc->base.name);

	crtc->active = false;
	crtc->base.enabled = false;

	if (crtc_state->intel_dpll)
		intel_dpll_crtc_put(crtc,
				    crtc_state->intel_dpll,
				    &crtc_state->intel_dpll->state);
}

static void set_encoder_for_connector(struct intel_connector *connector,
				      struct intel_encoder *encoder)
{
	struct drm_connector_state *conn_state = connector->base.state;

	if (conn_state->crtc)
		drm_connector_put(&connector->base);

	if (encoder) {
		conn_state->best_encoder = &encoder->base;
		conn_state->crtc = encoder->base.crtc;
		drm_connector_get(&connector->base);
	} else {
		conn_state->best_encoder = NULL;
		conn_state->crtc = NULL;
	}
}

static void reset_encoder_connector_state(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_pmdemand_state *pmdemand_state =
		to_intel_pmdemand_state(display->pmdemand.obj.state);
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (connector->base.encoder != &encoder->base)
			continue;

		/* Clear the corresponding bit in pmdemand active phys mask */
		intel_pmdemand_update_phys_mask(display, encoder,
						pmdemand_state, false);

		set_encoder_for_connector(connector, NULL);

		connector->base.dpms = DRM_MODE_DPMS_OFF;
		connector->base.encoder = NULL;
	}
	drm_connector_list_iter_end(&conn_iter);
}

static void reset_crtc_encoder_state(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(display->drm, &crtc->base, encoder) {
		reset_encoder_connector_state(encoder);
		encoder->base.crtc = NULL;
	}
}

static void intel_crtc_disable_noatomic_complete(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_pmdemand_state *pmdemand_state =
		to_intel_pmdemand_state(display->pmdemand.obj.state);
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	enum pipe pipe = crtc->pipe;

	__drm_atomic_helper_crtc_destroy_state(&crtc_state->uapi);
	intel_crtc_free_hw_state(crtc_state);
	intel_crtc_state_reset(crtc_state, crtc);

	reset_crtc_encoder_state(crtc);

	intel_fbc_disable(crtc);
	intel_update_watermarks(display);

	intel_display_power_put_all_in_set(display, &crtc->enabled_power_domains);

	intel_cdclk_crtc_disable_noatomic(crtc);
	skl_wm_crtc_disable_noatomic(crtc);
	intel_bw_crtc_disable_noatomic(crtc);

	intel_pmdemand_update_port_clock(display, pmdemand_state, pipe, 0);
}

/*
 * Return all the pipes using a transcoder in @transcoder_mask.
 * For joiner configs return only the joiner primary.
 */
static u8 get_transcoder_pipes(struct intel_display *display,
			       u8 transcoder_mask)
{
	struct intel_crtc *temp_crtc;
	u8 pipes = 0;

	for_each_intel_crtc(display->drm, temp_crtc) {
		struct intel_crtc_state *temp_crtc_state =
			to_intel_crtc_state(temp_crtc->base.state);

		if (temp_crtc_state->cpu_transcoder == INVALID_TRANSCODER)
			continue;

		if (intel_crtc_is_joiner_secondary(temp_crtc_state))
			continue;

		if (transcoder_mask & BIT(temp_crtc_state->cpu_transcoder))
			pipes |= BIT(temp_crtc->pipe);
	}

	return pipes;
}

/*
 * Return the port sync master and slave pipes linked to @crtc.
 * For joiner configs return only the joiner primary pipes.
 */
static void get_portsync_pipes(struct intel_crtc *crtc,
			       u8 *master_pipe_mask, u8 *slave_pipes_mask)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_crtc *master_crtc;
	struct intel_crtc_state *master_crtc_state;
	enum transcoder master_transcoder;

	if (!is_trans_port_sync_mode(crtc_state)) {
		*master_pipe_mask = BIT(crtc->pipe);
		*slave_pipes_mask = 0;

		return;
	}

	if (is_trans_port_sync_master(crtc_state))
		master_transcoder = crtc_state->cpu_transcoder;
	else
		master_transcoder = crtc_state->master_transcoder;

	*master_pipe_mask = get_transcoder_pipes(display, BIT(master_transcoder));
	drm_WARN_ON(display->drm, !is_power_of_2(*master_pipe_mask));

	master_crtc = intel_crtc_for_pipe(display, ffs(*master_pipe_mask) - 1);
	master_crtc_state = to_intel_crtc_state(master_crtc->base.state);
	*slave_pipes_mask = get_transcoder_pipes(display, master_crtc_state->sync_mode_slaves_mask);
}

static u8 get_joiner_secondary_pipes(struct intel_display *display, u8 primary_pipes_mask)
{
	struct intel_crtc *primary_crtc;
	u8 pipes = 0;

	for_each_intel_crtc_in_pipe_mask(display->drm, primary_crtc, primary_pipes_mask) {
		struct intel_crtc_state *primary_crtc_state =
			to_intel_crtc_state(primary_crtc->base.state);

		pipes |= intel_crtc_joiner_secondary_pipes(primary_crtc_state);
	}

	return pipes;
}

static void intel_crtc_disable_noatomic(struct intel_crtc *crtc,
					struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc *temp_crtc;
	u8 portsync_master_mask;
	u8 portsync_slaves_mask;
	u8 joiner_secondaries_mask;

	/* TODO: Add support for MST */
	get_portsync_pipes(crtc, &portsync_master_mask, &portsync_slaves_mask);
	joiner_secondaries_mask = get_joiner_secondary_pipes(display,
							     portsync_master_mask |
							     portsync_slaves_mask);

	drm_WARN_ON(display->drm,
		    portsync_master_mask & portsync_slaves_mask ||
		    portsync_master_mask & joiner_secondaries_mask ||
		    portsync_slaves_mask & joiner_secondaries_mask);

	for_each_intel_crtc_in_pipe_mask(display->drm, temp_crtc, joiner_secondaries_mask)
		intel_crtc_disable_noatomic_begin(temp_crtc, ctx);

	for_each_intel_crtc_in_pipe_mask(display->drm, temp_crtc, portsync_slaves_mask)
		intel_crtc_disable_noatomic_begin(temp_crtc, ctx);

	for_each_intel_crtc_in_pipe_mask(display->drm, temp_crtc, portsync_master_mask)
		intel_crtc_disable_noatomic_begin(temp_crtc, ctx);

	for_each_intel_crtc_in_pipe_mask(display->drm, temp_crtc,
					 joiner_secondaries_mask |
					 portsync_slaves_mask |
					 portsync_master_mask)
		intel_crtc_disable_noatomic_complete(temp_crtc);
}

static void intel_modeset_update_connector_atomic_state(struct intel_display *display)
{
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *conn_state = connector->base.state;
		struct intel_encoder *encoder =
			to_intel_encoder(connector->base.encoder);

		set_encoder_for_connector(connector, encoder);

		if (encoder) {
			struct intel_crtc *crtc =
				to_intel_crtc(encoder->base.crtc);
			const struct intel_crtc_state *crtc_state =
				to_intel_crtc_state(crtc->base.state);

			conn_state->max_bpc = (crtc_state->pipe_bpp ?: 24) / 3;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
}

static void intel_crtc_copy_hw_to_uapi_state(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (intel_crtc_is_joiner_secondary(crtc_state))
		return;

	crtc_state->uapi.enable = crtc_state->hw.enable;
	crtc_state->uapi.active = crtc_state->hw.active;
	drm_WARN_ON(crtc_state->uapi.crtc->dev,
		    drm_atomic_set_mode_for_crtc(&crtc_state->uapi, &crtc_state->hw.mode) < 0);

	crtc_state->uapi.adjusted_mode = crtc_state->hw.adjusted_mode;
	crtc_state->uapi.scaling_filter = crtc_state->hw.scaling_filter;

	if (DISPLAY_INFO(display)->color.degamma_lut_size) {
		/* assume 1:1 mapping */
		drm_property_replace_blob(&crtc_state->hw.degamma_lut,
					  crtc_state->pre_csc_lut);
		drm_property_replace_blob(&crtc_state->hw.gamma_lut,
					  crtc_state->post_csc_lut);
	} else {
		/*
		 * ilk/snb hw may be configured for either pre_csc_lut
		 * or post_csc_lut, but we don't advertise degamma_lut as
		 * being available in the uapi since there is only one
		 * hardware LUT. Always assign the result of the readout
		 * to gamma_lut as that is the only valid source of LUTs
		 * in the uapi.
		 */
		drm_WARN_ON(display->drm, crtc_state->post_csc_lut &&
			    crtc_state->pre_csc_lut);

		drm_property_replace_blob(&crtc_state->hw.degamma_lut,
					  NULL);
		drm_property_replace_blob(&crtc_state->hw.gamma_lut,
					  crtc_state->post_csc_lut ?:
					  crtc_state->pre_csc_lut);
	}

	drm_property_replace_blob(&crtc_state->uapi.degamma_lut,
				  crtc_state->hw.degamma_lut);
	drm_property_replace_blob(&crtc_state->uapi.gamma_lut,
				  crtc_state->hw.gamma_lut);
	drm_property_replace_blob(&crtc_state->uapi.ctm,
				  crtc_state->hw.ctm);
}

static void
intel_sanitize_plane_mapping(struct intel_display *display)
{
	struct intel_crtc *crtc;

	if (DISPLAY_VER(display) >= 4)
		return;

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_crtc *plane_crtc;
		enum pipe pipe;

		if (!plane->get_hw_state(plane, &pipe))
			continue;

		if (pipe == crtc->pipe)
			continue;

		drm_dbg_kms(display->drm,
			    "[PLANE:%d:%s] attached to the wrong pipe, disabling plane\n",
			    plane->base.base.id, plane->base.name);

		plane_crtc = intel_crtc_for_pipe(display, pipe);
		intel_plane_disable_noatomic(plane_crtc, plane);
	}
}

static bool intel_crtc_has_encoders(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(dev, &crtc->base, encoder)
		return true;

	return false;
}

static bool intel_crtc_needs_link_reset(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(dev, &crtc->base, encoder) {
		struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

		if (dig_port && intel_tc_port_link_needs_reset(dig_port))
			return true;
	}

	return false;
}

static struct intel_connector *intel_encoder_find_connector(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	struct intel_connector *found_connector = NULL;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (&encoder->base == connector->base.encoder) {
			found_connector = connector;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return found_connector;
}

static void intel_sanitize_fifo_underrun_reporting(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	/*
	 * We start out with underrun reporting disabled on active
	 * pipes to avoid races.
	 *
	 * Also on gmch platforms we dont have any hardware bits to
	 * disable the underrun reporting. Which means we need to start
	 * out with underrun reporting disabled also on inactive pipes,
	 * since otherwise we'll complain about the garbage we read when
	 * e.g. coming up after runtime pm.
	 *
	 * No protection against concurrent access is required - at
	 * worst a fifo underrun happens which also sets this to false.
	 */
	intel_init_fifo_underrun_reporting(display, crtc,
					   !crtc_state->hw.active &&
					   !HAS_GMCH(display));
}

static bool intel_sanitize_crtc(struct intel_crtc *crtc,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state = to_intel_crtc_state(crtc->base.state);
	bool needs_link_reset;

	if (crtc_state->hw.active) {
		struct intel_plane *plane;

		/* Disable everything but the primary plane */
		for_each_intel_plane_on_crtc(display->drm, crtc, plane) {
			const struct intel_plane_state *plane_state =
				to_intel_plane_state(plane->base.state);

			if (plane_state->uapi.visible &&
			    plane->base.type != DRM_PLANE_TYPE_PRIMARY)
				intel_plane_disable_noatomic(crtc, plane);
		}

		/* Disable any background color/etc. set by the BIOS */
		intel_color_commit_noarm(NULL, crtc_state);
		intel_color_commit_arm(NULL, crtc_state);
	}

	if (!crtc_state->hw.active ||
	    intel_crtc_is_joiner_secondary(crtc_state))
		return false;

	needs_link_reset = intel_crtc_needs_link_reset(crtc);

	/*
	 * Adjust the state of the output pipe according to whether we have
	 * active connectors/encoders.
	 */
	if (!needs_link_reset && intel_crtc_has_encoders(crtc))
		return false;

	intel_crtc_disable_noatomic(crtc, ctx);

	/*
	 * The HPD state on other active/disconnected TC ports may be stuck in
	 * the connected state until this port is disabled and a ~10ms delay has
	 * passed, wait here for that so that sanitizing other CRTCs will see the
	 * up-to-date HPD state.
	 */
	if (needs_link_reset)
		msleep(20);

	return true;
}

static void intel_sanitize_all_crtcs(struct intel_display *display,
				     struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_crtc *crtc;
	u32 crtcs_forced_off = 0;

	/*
	 * An active and disconnected TypeC port prevents the HPD live state
	 * to get updated on other active/disconnected TypeC ports, so after
	 * a port gets disabled the CRTCs using other TypeC ports must be
	 * rechecked wrt. their link status.
	 */
	for (;;) {
		u32 old_mask = crtcs_forced_off;

		for_each_intel_crtc(display->drm, crtc) {
			u32 crtc_mask = drm_crtc_mask(&crtc->base);

			if (crtcs_forced_off & crtc_mask)
				continue;

			if (intel_sanitize_crtc(crtc, ctx))
				crtcs_forced_off |= crtc_mask;
		}
		if (crtcs_forced_off == old_mask)
			break;
	}

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		intel_crtc_state_dump(crtc_state, NULL, "setup_hw_state");
	}
}

static bool has_bogus_dpll_config(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/*
	 * Some SNB BIOSen (eg. ASUS K53SV) are known to misprogram
	 * the hardware when a high res displays plugged in. DPLL P
	 * divider is zero, and the pipe timings are bonkers. We'll
	 * try to disable everything in that case.
	 *
	 * FIXME would be nice to be able to sanitize this state
	 * without several WARNs, but for now let's take the easy
	 * road.
	 */
	return display->platform.sandybridge &&
		crtc_state->hw.active &&
		crtc_state->intel_dpll &&
		crtc_state->port_clock == 0;
}

static void intel_sanitize_encoder(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_connector *connector;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_crtc_state *crtc_state = crtc ?
		to_intel_crtc_state(crtc->base.state) : NULL;
	struct intel_pmdemand_state *pmdemand_state =
		to_intel_pmdemand_state(display->pmdemand.obj.state);

	/*
	 * We need to check both for a crtc link (meaning that the encoder is
	 * active and trying to read from a pipe) and the pipe itself being
	 * active.
	 */
	bool has_active_crtc = crtc_state &&
		crtc_state->hw.active;

	if (crtc_state && has_bogus_dpll_config(crtc_state)) {
		drm_dbg_kms(display->drm,
			    "BIOS has misprogrammed the hardware. Disabling pipe %c\n",
			    pipe_name(crtc->pipe));
		has_active_crtc = false;
	}

	connector = intel_encoder_find_connector(encoder);
	if (connector && !has_active_crtc) {
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] has active connectors but no active pipe!\n",
			    encoder->base.base.id,
			    encoder->base.name);

		/* Clear the corresponding bit in pmdemand active phys mask */
		intel_pmdemand_update_phys_mask(display, encoder,
						pmdemand_state, false);

		/*
		 * Connector is active, but has no active pipe. This is fallout
		 * from our resume register restoring. Disable the encoder
		 * manually again.
		 */
		if (crtc_state) {
			struct drm_encoder *best_encoder;

			drm_dbg_kms(display->drm,
				    "[ENCODER:%d:%s] manually disabled\n",
				    encoder->base.base.id,
				    encoder->base.name);

			/* avoid oopsing in case the hooks consult best_encoder */
			best_encoder = connector->base.state->best_encoder;
			connector->base.state->best_encoder = &encoder->base;

			/* FIXME NULL atomic state passed! */
			if (encoder->disable)
				encoder->disable(NULL, encoder, crtc_state,
						 connector->base.state);
			if (encoder->post_disable)
				encoder->post_disable(NULL, encoder, crtc_state,
						      connector->base.state);

			connector->base.state->best_encoder = best_encoder;
		}
		encoder->base.crtc = NULL;

		/*
		 * Inconsistent output/port/pipe state happens presumably due to
		 * a bug in one of the get_hw_state functions. Or someplace else
		 * in our code, like the register restore mess on resume. Clamp
		 * things to off as a safer default.
		 */
		connector->base.dpms = DRM_MODE_DPMS_OFF;
		connector->base.encoder = NULL;
	}

	/* notify opregion of the sanitized encoder state */
	intel_opregion_notify_encoder(encoder, connector && has_active_crtc);

	if (HAS_DDI(display))
		intel_ddi_sanitize_encoder_pll_mapping(encoder);
}

/* FIXME read out full plane state for all planes */
static void readout_plane_state(struct intel_display *display)
{
	struct intel_plane *plane;
	struct intel_crtc *crtc;

	for_each_intel_plane(display->drm, plane) {
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct intel_crtc_state *crtc_state;
		enum pipe pipe = PIPE_A;
		bool visible;

		visible = plane->get_hw_state(plane, &pipe);

		crtc = intel_crtc_for_pipe(display, pipe);
		crtc_state = to_intel_crtc_state(crtc->base.state);

		intel_set_plane_visible(crtc_state, plane_state, visible);

		drm_dbg_kms(display->drm,
			    "[PLANE:%d:%s] hw state readout: %s, pipe %c\n",
			    plane->base.base.id, plane->base.name,
			    str_enabled_disabled(visible), pipe_name(pipe));
	}

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		intel_plane_fixup_bitmasks(crtc_state);
	}
}

static void intel_modeset_readout_hw_state(struct intel_display *display)
{
	struct intel_pmdemand_state *pmdemand_state =
		to_intel_pmdemand_state(display->pmdemand.obj.state);
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		__drm_atomic_helper_crtc_destroy_state(&crtc_state->uapi);
		intel_crtc_free_hw_state(crtc_state);
		intel_crtc_state_reset(crtc_state, crtc);

		intel_crtc_get_pipe_config(crtc_state);

		crtc_state->hw.enable = crtc_state->hw.active;

		crtc->base.enabled = crtc_state->hw.enable;
		crtc->active = crtc_state->hw.active;

		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] hw state readout: %s\n",
			    crtc->base.base.id, crtc->base.name,
			    str_enabled_disabled(crtc_state->hw.active));
	}

	readout_plane_state(display);

	for_each_intel_encoder(display->drm, encoder) {
		struct intel_crtc_state *crtc_state = NULL;

		pipe = 0;

		if (encoder->get_hw_state(encoder, &pipe)) {
			crtc = intel_crtc_for_pipe(display, pipe);
			crtc_state = to_intel_crtc_state(crtc->base.state);

			encoder->base.crtc = &crtc->base;
			intel_encoder_get_config(encoder, crtc_state);

			/* read out to secondary crtc as well for joiner */
			if (crtc_state->joiner_pipes) {
				struct intel_crtc *secondary_crtc;

				/* encoder should read be linked to joiner primary */
				WARN_ON(intel_crtc_is_joiner_secondary(crtc_state));

				for_each_intel_crtc_in_pipe_mask(display->drm, secondary_crtc,
								 intel_crtc_joiner_secondary_pipes(crtc_state)) {
					struct intel_crtc_state *secondary_crtc_state;

					secondary_crtc_state = to_intel_crtc_state(secondary_crtc->base.state);
					intel_encoder_get_config(encoder, secondary_crtc_state);
				}
			}

			intel_pmdemand_update_phys_mask(display, encoder,
							pmdemand_state,
							true);
		} else {
			intel_pmdemand_update_phys_mask(display, encoder,
							pmdemand_state,
							false);

			encoder->base.crtc = NULL;
		}

		if (encoder->sync_state)
			encoder->sync_state(encoder, crtc_state);

		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] hw state readout: %s, pipe %c\n",
			    encoder->base.base.id, encoder->base.name,
			    str_enabled_disabled(encoder->base.crtc),
			    pipe_name(pipe));
	}

	intel_dpll_readout_hw_state(display);

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		struct intel_crtc_state *crtc_state = NULL;

		if (connector->get_hw_state(connector)) {
			struct intel_crtc *crtc;

			connector->base.dpms = DRM_MODE_DPMS_ON;

			encoder = intel_attached_encoder(connector);
			connector->base.encoder = &encoder->base;

			crtc = to_intel_crtc(encoder->base.crtc);
			crtc_state = crtc ? to_intel_crtc_state(crtc->base.state) : NULL;

			if (crtc_state && crtc_state->hw.active) {
				/*
				 * This has to be done during hardware readout
				 * because anything calling .crtc_disable may
				 * rely on the connector_mask being accurate.
				 */
				crtc_state->uapi.connector_mask |=
					drm_connector_mask(&connector->base);
				crtc_state->uapi.encoder_mask |=
					drm_encoder_mask(&encoder->base);
			}
		} else {
			connector->base.dpms = DRM_MODE_DPMS_OFF;
			connector->base.encoder = NULL;
		}

		if (connector->sync_state)
			connector->sync_state(connector, crtc_state);

		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] hw state readout: %s\n",
			    connector->base.base.id, connector->base.name,
			    str_enabled_disabled(connector->base.encoder));
	}
	drm_connector_list_iter_end(&conn_iter);

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane;

		/*
		 * The initial mode needs to be set in order to keep
		 * the atomic core happy. It wants a valid mode if the
		 * crtc's enabled, so we do the above call.
		 *
		 * But we don't set all the derived state fully, hence
		 * set a flag to indicate that a full recalculation is
		 * needed on the next commit.
		 */
		crtc_state->inherited = true;

		if (crtc_state->hw.active) {
			intel_crtc_update_active_timings(crtc_state,
							 crtc_state->vrr.enable);

			intel_crtc_copy_hw_to_uapi_state(crtc_state);
		}

		for_each_intel_plane_on_crtc(display->drm, crtc, plane) {
			const struct intel_plane_state *plane_state =
				to_intel_plane_state(plane->base.state);

			/*
			 * FIXME don't have the fb yet, so can't
			 * use intel_plane_data_rate() :(
			 */
			if (plane_state->uapi.visible)
				crtc_state->data_rate[plane->id] =
					4 * crtc_state->pixel_rate;
			/*
			 * FIXME don't have the fb yet, so can't
			 * use plane->min_cdclk() :(
			 */
			if (plane_state->uapi.visible && plane->min_cdclk) {
				if (crtc_state->double_wide || DISPLAY_VER(display) >= 10)
					crtc_state->min_cdclk[plane->id] =
						DIV_ROUND_UP(crtc_state->pixel_rate, 2);
				else
					crtc_state->min_cdclk[plane->id] =
						crtc_state->pixel_rate;
			}
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] min_cdclk %d kHz\n",
				    plane->base.base.id, plane->base.name,
				    crtc_state->min_cdclk[plane->id]);
		}

		intel_pmdemand_update_port_clock(display, pmdemand_state, pipe,
						 crtc_state->port_clock);
	}

	/* TODO move here (or even earlier?) on all platforms */
	if (DISPLAY_VER(display) >= 9)
		intel_wm_get_hw_state(display);

	intel_bw_update_hw_state(display);
	intel_cdclk_update_hw_state(display);

	intel_pmdemand_init_pmdemand_params(display, pmdemand_state);
}

static void
get_encoder_power_domains(struct intel_display *display)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(display->drm, encoder) {
		struct intel_crtc_state *crtc_state;

		if (!encoder->get_power_domains)
			continue;

		/*
		 * MST-primary and inactive encoders don't have a crtc state
		 * and neither of these require any power domain references.
		 */
		if (!encoder->base.crtc)
			continue;

		crtc_state = to_intel_crtc_state(encoder->base.crtc->state);
		encoder->get_power_domains(encoder, crtc_state);
	}
}

static void intel_early_display_was(struct intel_display *display)
{
	/*
	 * Display WA #1185 WaDisableDARBFClkGating:glk,icl,ehl,tgl
	 * Also known as Wa_14010480278.
	 */
	if (IS_DISPLAY_VER(display, 10, 12))
		intel_de_rmw(display, GEN9_CLKGATE_DIS_0, 0, DARBF_GATING_DIS);

	/*
	 * WaRsPkgCStateDisplayPMReq:hsw
	 * System hang if this isn't done before disabling all planes!
	 */
	if (display->platform.haswell)
		intel_de_rmw(display, CHICKEN_PAR1_1, 0, FORCE_ARB_IDLE_PLANES);

	if (display->platform.kabylake || display->platform.coffeelake ||
	    display->platform.cometlake) {
		/* Display WA #1142:kbl,cfl,cml */
		intel_de_rmw(display, CHICKEN_PAR1_1,
			     KBL_ARB_FILL_SPARE_22, KBL_ARB_FILL_SPARE_22);
		intel_de_rmw(display, CHICKEN_MISC_2,
			     KBL_ARB_FILL_SPARE_13 | KBL_ARB_FILL_SPARE_14,
			     KBL_ARB_FILL_SPARE_14);
	}
}

void intel_modeset_setup_hw_state(struct intel_display *display,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	intel_wakeref_t wakeref;

	wakeref = intel_display_power_get(display, POWER_DOMAIN_INIT);

	intel_early_display_was(display);
	intel_vga_disable(display);

	intel_modeset_readout_hw_state(display);

	/* HW state is read out, now we need to sanitize this mess. */
	get_encoder_power_domains(display);

	intel_pch_sanitize(display);

	intel_cmtg_sanitize(display);

	/*
	 * intel_sanitize_plane_mapping() may need to do vblank
	 * waits, so we need vblank interrupts restored beforehand.
	 */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		intel_sanitize_fifo_underrun_reporting(crtc_state);

		drm_crtc_vblank_reset(&crtc->base);

		if (crtc_state->hw.active) {
			intel_dmc_enable_pipe(crtc_state);
			intel_crtc_vblank_on(crtc_state);
		}
	}

	intel_fbc_sanitize(display);

	intel_sanitize_plane_mapping(display);

	for_each_intel_encoder(display->drm, encoder)
		intel_sanitize_encoder(encoder);

	/*
	 * Sanitizing CRTCs needs their connector atomic state to be
	 * up-to-date, so ensure that already here.
	 */
	intel_modeset_update_connector_atomic_state(display);

	intel_sanitize_all_crtcs(display, ctx);

	intel_dpll_sanitize_state(display);

	/* TODO move earlier on all platforms */
	if (DISPLAY_VER(display) < 9)
		intel_wm_get_hw_state(display);
	intel_wm_sanitize(display);

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_power_domain_mask put_domains;

		intel_modeset_get_crtc_power_domains(crtc_state, &put_domains);
		if (drm_WARN_ON(display->drm, !bitmap_empty(put_domains.bits, POWER_DOMAIN_NUM)))
			intel_modeset_put_crtc_power_domains(crtc, &put_domains);
	}

	intel_display_power_put(display, POWER_DOMAIN_INIT, wakeref);

	intel_power_domains_sanitize_state(display);
}
