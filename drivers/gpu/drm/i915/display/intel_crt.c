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

#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "intel_connector.h"
#include "intel_crt.h"
#include "intel_crt_regs.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_ddi_buf_trans.h"
#include "intel_de.h"
#include "intel_display_driver.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_fdi.h"
#include "intel_fdi_regs.h"
#include "intel_fifo_underrun.h"
#include "intel_gmbus.h"
#include "intel_hotplug.h"
#include "intel_hotplug_irq.h"
#include "intel_load_detect.h"
#include "intel_pch_display.h"
#include "intel_pch_refclk.h"
#include "intel_pfit.h"

/* Here's the desired hotplug mode */
#define ADPA_HOTPLUG_BITS (ADPA_CRT_HOTPLUG_ENABLE |			\
			   ADPA_CRT_HOTPLUG_PERIOD_128 |		\
			   ADPA_CRT_HOTPLUG_WARMUP_10MS |		\
			   ADPA_CRT_HOTPLUG_SAMPLE_4S |			\
			   ADPA_CRT_HOTPLUG_VOLTAGE_50 |		\
			   ADPA_CRT_HOTPLUG_VOLREF_325MV)
#define ADPA_HOTPLUG_MASK (ADPA_CRT_HOTPLUG_MONITOR_MASK |		\
			   ADPA_CRT_HOTPLUG_ENABLE |			\
			   ADPA_CRT_HOTPLUG_PERIOD_MASK |		\
			   ADPA_CRT_HOTPLUG_WARMUP_MASK |		\
			   ADPA_CRT_HOTPLUG_SAMPLE_MASK |		\
			   ADPA_CRT_HOTPLUG_VOLTAGE_MASK |		\
			   ADPA_CRT_HOTPLUG_VOLREF_MASK |		\
			   ADPA_CRT_HOTPLUG_FORCE_TRIGGER)

struct intel_crt {
	struct intel_encoder base;
	bool force_hotplug_required;
	i915_reg_t adpa_reg;
};

static struct intel_crt *intel_encoder_to_crt(struct intel_encoder *encoder)
{
	return container_of(encoder, struct intel_crt, base);
}

static struct intel_crt *intel_attached_crt(struct intel_connector *connector)
{
	return intel_encoder_to_crt(intel_attached_encoder(connector));
}

bool intel_crt_port_enabled(struct intel_display *display,
			    i915_reg_t adpa_reg, enum pipe *pipe)
{
	u32 val;

	val = intel_de_read(display, adpa_reg);

	/* asserts want to know the pipe even if the port is disabled */
	if (HAS_PCH_CPT(display))
		*pipe = REG_FIELD_GET(ADPA_PIPE_SEL_MASK_CPT, val);
	else
		*pipe = REG_FIELD_GET(ADPA_PIPE_SEL_MASK, val);

	return val & ADPA_DAC_ENABLE;
}

static bool intel_crt_get_hw_state(struct intel_encoder *encoder,
				   enum pipe *pipe)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crt *crt = intel_encoder_to_crt(encoder);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(display,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	ret = intel_crt_port_enabled(display, crt->adpa_reg, pipe);

	intel_display_power_put(display, encoder->power_domain, wakeref);

	return ret;
}

static unsigned int intel_crt_get_flags(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crt *crt = intel_encoder_to_crt(encoder);
	u32 tmp, flags = 0;

	tmp = intel_de_read(display, crt->adpa_reg);

	if (tmp & ADPA_HSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;

	if (tmp & ADPA_VSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	return flags;
}

static void intel_crt_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state)
{
	crtc_state->output_types |= BIT(INTEL_OUTPUT_ANALOG);

	crtc_state->hw.adjusted_mode.flags |= intel_crt_get_flags(encoder);

	crtc_state->hw.adjusted_mode.crtc_clock = crtc_state->port_clock;
}

static void hsw_crt_get_config(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state)
{
	lpt_pch_get_config(crtc_state);

	hsw_ddi_get_config(encoder, crtc_state);

	crtc_state->hw.adjusted_mode.flags &= ~(DRM_MODE_FLAG_PHSYNC |
						DRM_MODE_FLAG_NHSYNC |
						DRM_MODE_FLAG_PVSYNC |
						DRM_MODE_FLAG_NVSYNC);
	crtc_state->hw.adjusted_mode.flags |= intel_crt_get_flags(encoder);
}

/* Note: The caller is required to filter out dpms modes not supported by the
 * platform. */
static void intel_crt_set_dpms(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       int mode)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crt *crt = intel_encoder_to_crt(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	u32 adpa;

	if (DISPLAY_VER(display) >= 5)
		adpa = ADPA_HOTPLUG_BITS;
	else
		adpa = 0;

	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		adpa |= ADPA_HSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		adpa |= ADPA_VSYNC_ACTIVE_HIGH;

	/* For CPT allow 3 pipe config, for others just use A or B */
	if (HAS_PCH_LPT(display))
		; /* Those bits don't exist here */
	else if (HAS_PCH_CPT(display))
		adpa |= ADPA_PIPE_SEL_CPT(crtc->pipe);
	else
		adpa |= ADPA_PIPE_SEL(crtc->pipe);

	if (!HAS_PCH_SPLIT(display))
		intel_de_write(display, BCLRPAT(display, crtc->pipe), 0);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		adpa |= ADPA_DAC_ENABLE;
		break;
	case DRM_MODE_DPMS_STANDBY:
		adpa |= ADPA_DAC_ENABLE | ADPA_HSYNC_CNTL_DISABLE;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		adpa |= ADPA_DAC_ENABLE | ADPA_VSYNC_CNTL_DISABLE;
		break;
	case DRM_MODE_DPMS_OFF:
		adpa |= ADPA_HSYNC_CNTL_DISABLE | ADPA_VSYNC_CNTL_DISABLE;
		break;
	}

	intel_de_write(display, crt->adpa_reg, adpa);
}

static void intel_disable_crt(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	intel_crt_set_dpms(encoder, old_crtc_state, DRM_MODE_DPMS_OFF);
}

static void pch_disable_crt(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *old_crtc_state,
			    const struct drm_connector_state *old_conn_state)
{
}

static void pch_post_disable_crt(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	intel_disable_crt(state, encoder, old_crtc_state, old_conn_state);
}

static void hsw_disable_crt(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *old_crtc_state,
			    const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(encoder);

	drm_WARN_ON(display->drm, !old_crtc_state->has_pch_encoder);

	intel_set_pch_fifo_underrun_reporting(display, PIPE_A, false);
}

static void hsw_post_disable_crt(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);

	intel_crtc_vblank_off(old_crtc_state);

	intel_disable_transcoder(old_crtc_state);

	intel_ddi_disable_transcoder_func(old_crtc_state);

	ilk_pfit_disable(old_crtc_state);

	intel_ddi_disable_transcoder_clock(old_crtc_state);

	pch_post_disable_crt(state, encoder, old_crtc_state, old_conn_state);

	lpt_pch_disable(state, crtc);

	hsw_fdi_disable(encoder);

	drm_WARN_ON(display->drm, !old_crtc_state->has_pch_encoder);

	intel_set_pch_fifo_underrun_reporting(display, PIPE_A, true);
}

static void hsw_pre_pll_enable_crt(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);

	drm_WARN_ON(display->drm, !crtc_state->has_pch_encoder);

	intel_set_pch_fifo_underrun_reporting(display, PIPE_A, false);
}

static void hsw_pre_enable_crt(struct intel_atomic_state *state,
			       struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	drm_WARN_ON(display->drm, !crtc_state->has_pch_encoder);

	intel_set_cpu_fifo_underrun_reporting(display, pipe, false);

	hsw_fdi_link_train(encoder, crtc_state);

	intel_ddi_enable_transcoder_clock(encoder, crtc_state);
}

static void hsw_enable_crt(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	drm_WARN_ON(display->drm, !crtc_state->has_pch_encoder);

	intel_ddi_enable_transcoder_func(encoder, crtc_state);

	intel_enable_transcoder(crtc_state);

	lpt_pch_enable(state, crtc);

	intel_crtc_vblank_on(crtc_state);

	intel_crt_set_dpms(encoder, crtc_state, DRM_MODE_DPMS_ON);

	intel_crtc_wait_for_next_vblank(crtc);
	intel_crtc_wait_for_next_vblank(crtc);
	intel_set_cpu_fifo_underrun_reporting(display, pipe, true);
	intel_set_pch_fifo_underrun_reporting(display, PIPE_A, true);
}

static void intel_enable_crt(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	intel_crt_set_dpms(encoder, crtc_state, DRM_MODE_DPMS_ON);
}

static enum drm_mode_status
intel_crt_mode_valid(struct drm_connector *connector,
		     const struct drm_display_mode *mode)
{
	struct intel_display *display = to_intel_display(connector->dev);
	int max_dotclk = display->cdclk.max_dotclk_freq;
	enum drm_mode_status status;
	int max_clock;

	status = intel_cpu_transcoder_mode_valid(display, mode);
	if (status != MODE_OK)
		return status;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	if (HAS_PCH_LPT(display))
		max_clock = 180000;
	else if (display->platform.valleyview)
		/*
		 * 270 MHz due to current DPLL limits,
		 * DAC limit supposedly 355 MHz.
		 */
		max_clock = 270000;
	else if (IS_DISPLAY_VER(display, 3, 4))
		max_clock = 400000;
	else
		max_clock = 350000;
	if (mode->clock > max_clock)
		return MODE_CLOCK_HIGH;

	if (mode->clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	/* The FDI receiver on LPT only supports 8bpc and only has 2 lanes. */
	if (HAS_PCH_LPT(display) &&
	    ilk_get_lanes_required(mode->clock, 270000, 24) > 2)
		return MODE_CLOCK_HIGH;

	/* HSW/BDW FDI limited to 4k */
	if (mode->hdisplay > 4096)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

static int intel_crt_compute_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	crtc_state->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;

	return 0;
}

static int pch_crt_compute_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	crtc_state->has_pch_encoder = true;
	if (!intel_fdi_compute_pipe_bpp(crtc_state))
		return -EINVAL;

	crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;

	return 0;
}

static int hsw_crt_compute_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	/* HSW/BDW FDI limited to 4k */
	if (adjusted_mode->crtc_hdisplay > 4096 ||
	    adjusted_mode->crtc_hblank_start > 4096)
		return -EINVAL;

	crtc_state->has_pch_encoder = true;
	if (!intel_fdi_compute_pipe_bpp(crtc_state))
		return -EINVAL;

	crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;

	/* LPT FDI RX only supports 8bpc. */
	if (HAS_PCH_LPT(display)) {
		/* TODO: Check crtc_state->max_link_bpp_x16 instead of bw_constrained */
		if (crtc_state->bw_constrained && crtc_state->pipe_bpp < 24) {
			drm_dbg_kms(display->drm,
				    "LPT only supports 24bpp\n");
			return -EINVAL;
		}

		crtc_state->pipe_bpp = 24;
	}

	/* FDI must always be 2.7 GHz */
	crtc_state->port_clock = 135000 * 2;

	crtc_state->enhanced_framing = true;

	adjusted_mode->crtc_clock = lpt_iclkip(crtc_state);

	return 0;
}

static bool ilk_crt_detect_hotplug(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_crt *crt = intel_attached_crt(to_intel_connector(connector));
	u32 adpa;
	bool ret;

	/* The first time through, trigger an explicit detection cycle */
	if (crt->force_hotplug_required) {
		bool turn_off_dac = HAS_PCH_SPLIT(display);
		u32 save_adpa;

		crt->force_hotplug_required = false;

		save_adpa = adpa = intel_de_read(display, crt->adpa_reg);
		drm_dbg_kms(display->drm,
			    "trigger hotplug detect cycle: adpa=0x%x\n", adpa);

		adpa |= ADPA_CRT_HOTPLUG_FORCE_TRIGGER;
		if (turn_off_dac)
			adpa &= ~ADPA_DAC_ENABLE;

		intel_de_write(display, crt->adpa_reg, adpa);

		if (intel_de_wait_for_clear(display,
					    crt->adpa_reg,
					    ADPA_CRT_HOTPLUG_FORCE_TRIGGER,
					    1000))
			drm_dbg_kms(display->drm,
				    "timed out waiting for FORCE_TRIGGER");

		if (turn_off_dac) {
			intel_de_write(display, crt->adpa_reg, save_adpa);
			intel_de_posting_read(display, crt->adpa_reg);
		}
	}

	/* Check the status to see if both blue and green are on now */
	adpa = intel_de_read(display, crt->adpa_reg);
	if ((adpa & ADPA_CRT_HOTPLUG_MONITOR_MASK) != 0)
		ret = true;
	else
		ret = false;
	drm_dbg_kms(display->drm, "ironlake hotplug adpa=0x%x, result %d\n",
		    adpa, ret);

	return ret;
}

static bool valleyview_crt_detect_hotplug(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_crt *crt = intel_attached_crt(to_intel_connector(connector));
	u32 adpa;
	bool ret;
	u32 save_adpa;

	/*
	 * Doing a force trigger causes a hpd interrupt to get sent, which can
	 * get us stuck in a loop if we're polling:
	 *  - We enable power wells and reset the ADPA
	 *  - output_poll_exec does force probe on VGA, triggering a hpd
	 *  - HPD handler waits for poll to unlock dev->mode_config.mutex
	 *  - output_poll_exec shuts off the ADPA, unlocks
	 *    dev->mode_config.mutex
	 *  - HPD handler runs, resets ADPA and brings us back to the start
	 *
	 * Just disable HPD interrupts here to prevent this
	 */
	intel_hpd_block(&crt->base);

	save_adpa = adpa = intel_de_read(display, crt->adpa_reg);
	drm_dbg_kms(display->drm,
		    "trigger hotplug detect cycle: adpa=0x%x\n", adpa);

	adpa |= ADPA_CRT_HOTPLUG_FORCE_TRIGGER;

	intel_de_write(display, crt->adpa_reg, adpa);

	if (intel_de_wait_for_clear(display, crt->adpa_reg,
				    ADPA_CRT_HOTPLUG_FORCE_TRIGGER, 1000)) {
		drm_dbg_kms(display->drm,
			    "timed out waiting for FORCE_TRIGGER");
		intel_de_write(display, crt->adpa_reg, save_adpa);
	}

	/* Check the status to see if both blue and green are on now */
	adpa = intel_de_read(display, crt->adpa_reg);
	if ((adpa & ADPA_CRT_HOTPLUG_MONITOR_MASK) != 0)
		ret = true;
	else
		ret = false;

	drm_dbg_kms(display->drm,
		    "valleyview hotplug adpa=0x%x, result %d\n", adpa, ret);

	intel_hpd_clear_and_unblock(&crt->base);

	return ret;
}

static bool intel_crt_detect_hotplug(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	u32 stat;
	bool ret = false;
	int i, tries = 0;

	if (HAS_PCH_SPLIT(display))
		return ilk_crt_detect_hotplug(connector);

	if (display->platform.valleyview)
		return valleyview_crt_detect_hotplug(connector);

	/*
	 * On 4 series desktop, CRT detect sequence need to be done twice
	 * to get a reliable result.
	 */

	if (display->platform.g45)
		tries = 2;
	else
		tries = 1;

	for (i = 0; i < tries ; i++) {
		/* turn on the FORCE_DETECT */
		i915_hotplug_interrupt_update(display,
					      CRT_HOTPLUG_FORCE_DETECT,
					      CRT_HOTPLUG_FORCE_DETECT);
		/* wait for FORCE_DETECT to go off */
		if (intel_de_wait_for_clear(display, PORT_HOTPLUG_EN(display),
					    CRT_HOTPLUG_FORCE_DETECT, 1000))
			drm_dbg_kms(display->drm,
				    "timed out waiting for FORCE_DETECT to go off");
	}

	stat = intel_de_read(display, PORT_HOTPLUG_STAT(display));
	if ((stat & CRT_HOTPLUG_MONITOR_MASK) != CRT_HOTPLUG_MONITOR_NONE)
		ret = true;

	/* clear the interrupt we just generated, if any */
	intel_de_write(display, PORT_HOTPLUG_STAT(display),
		       CRT_HOTPLUG_INT_STATUS);

	i915_hotplug_interrupt_update(display, CRT_HOTPLUG_FORCE_DETECT, 0);

	return ret;
}

static const struct drm_edid *intel_crt_get_edid(struct drm_connector *connector,
						 struct i2c_adapter *ddc)
{
	const struct drm_edid *drm_edid;

	drm_edid = drm_edid_read_ddc(connector, ddc);

	if (!drm_edid && !intel_gmbus_is_forced_bit(ddc)) {
		drm_dbg_kms(connector->dev,
			    "CRT GMBUS EDID read failed, retry using GPIO bit-banging\n");
		intel_gmbus_force_bit(ddc, true);
		drm_edid = drm_edid_read_ddc(connector, ddc);
		intel_gmbus_force_bit(ddc, false);
	}

	return drm_edid;
}

/* local version of intel_ddc_get_modes() to use intel_crt_get_edid() */
static int intel_crt_ddc_get_modes(struct drm_connector *connector,
				   struct i2c_adapter *ddc)
{
	const struct drm_edid *drm_edid;
	int ret;

	drm_edid = intel_crt_get_edid(connector, ddc);
	if (!drm_edid)
		return 0;

	ret = intel_connector_update_modes(connector, drm_edid);

	drm_edid_free(drm_edid);

	return ret;
}

static bool intel_crt_detect_ddc(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	const struct drm_edid *drm_edid;
	bool ret = false;

	drm_edid = intel_crt_get_edid(connector, connector->ddc);

	if (drm_edid) {
		/*
		 * This may be a DVI-I connector with a shared DDC
		 * link between analog and digital outputs, so we
		 * have to check the EDID input spec of the attached device.
		 */
		if (drm_edid_is_digital(drm_edid)) {
			drm_dbg_kms(display->drm,
				    "CRT not detected via DDC:0x50 [EDID reports a digital panel]\n");
		} else {
			drm_dbg_kms(display->drm,
				    "CRT detected via DDC:0x50 [EDID]\n");
			ret = true;
		}
	} else {
		drm_dbg_kms(display->drm,
			    "CRT not detected via DDC:0x50 [no valid EDID found]\n");
	}

	drm_edid_free(drm_edid);

	return ret;
}

static enum drm_connector_status
intel_crt_load_detect(struct intel_crt *crt, enum pipe pipe)
{
	struct intel_display *display = to_intel_display(&crt->base);
	enum transcoder cpu_transcoder = (enum transcoder)pipe;
	u32 save_bclrpat;
	u32 save_vtotal;
	u32 vtotal, vactive;
	u32 vsample;
	u32 vblank, vblank_start, vblank_end;
	u32 dsl;
	u8 st00;
	enum drm_connector_status status;

	drm_dbg_kms(display->drm, "starting load-detect on CRT\n");

	save_bclrpat = intel_de_read(display,
				     BCLRPAT(display, cpu_transcoder));
	save_vtotal = intel_de_read(display,
				    TRANS_VTOTAL(display, cpu_transcoder));
	vblank = intel_de_read(display,
			       TRANS_VBLANK(display, cpu_transcoder));

	vtotal = REG_FIELD_GET(VTOTAL_MASK, save_vtotal) + 1;
	vactive = REG_FIELD_GET(VACTIVE_MASK, save_vtotal) + 1;

	vblank_start = REG_FIELD_GET(VBLANK_START_MASK, vblank) + 1;
	vblank_end = REG_FIELD_GET(VBLANK_END_MASK, vblank) + 1;

	/* Set the border color to purple. */
	intel_de_write(display, BCLRPAT(display, cpu_transcoder), 0x500050);

	if (DISPLAY_VER(display) != 2) {
		u32 transconf = intel_de_read(display,
					      TRANSCONF(display, cpu_transcoder));

		intel_de_write(display, TRANSCONF(display, cpu_transcoder),
			       transconf | TRANSCONF_FORCE_BORDER);
		intel_de_posting_read(display,
				      TRANSCONF(display, cpu_transcoder));
		/*
		 * Wait for next Vblank to substitute
		 * border color for Color info.
		 */
		intel_crtc_wait_for_next_vblank(intel_crtc_for_pipe(display, pipe));
		st00 = intel_de_read8(display, _VGA_MSR_WRITE);
		status = ((st00 & (1 << 4)) != 0) ?
			connector_status_connected :
			connector_status_disconnected;

		intel_de_write(display, TRANSCONF(display, cpu_transcoder),
			       transconf);
	} else {
		bool restore_vblank = false;
		int count, detect;

		/*
		* If there isn't any border, add some.
		* Yes, this will flicker
		*/
		if (vblank_start <= vactive && vblank_end >= vtotal) {
			u32 vsync = intel_de_read(display,
						  TRANS_VSYNC(display, cpu_transcoder));
			u32 vsync_start = REG_FIELD_GET(VSYNC_START_MASK, vsync) + 1;

			vblank_start = vsync_start;
			intel_de_write(display,
				       TRANS_VBLANK(display, cpu_transcoder),
				       VBLANK_START(vblank_start - 1) |
				       VBLANK_END(vblank_end - 1));
			restore_vblank = true;
		}
		/* sample in the vertical border, selecting the larger one */
		if (vblank_start - vactive >= vtotal - vblank_end)
			vsample = (vblank_start + vactive) >> 1;
		else
			vsample = (vtotal + vblank_end) >> 1;

		/*
		 * Wait for the border to be displayed
		 */
		while (intel_de_read(display, PIPEDSL(display, pipe)) >= vactive)
			;
		while ((dsl = intel_de_read(display, PIPEDSL(display, pipe))) <= vsample)
			;
		/*
		 * Watch ST00 for an entire scanline
		 */
		detect = 0;
		count = 0;
		do {
			count++;
			/* Read the ST00 VGA status register */
			st00 = intel_de_read8(display, _VGA_MSR_WRITE);
			if (st00 & (1 << 4))
				detect++;
		} while ((intel_de_read(display, PIPEDSL(display, pipe)) == dsl));

		/* restore vblank if necessary */
		if (restore_vblank)
			intel_de_write(display,
				       TRANS_VBLANK(display, cpu_transcoder),
				       vblank);
		/*
		 * If more than 3/4 of the scanline detected a monitor,
		 * then it is assumed to be present. This works even on i830,
		 * where there isn't any way to force the border color across
		 * the screen
		 */
		status = detect * 4 > count * 3 ?
			 connector_status_connected :
			 connector_status_disconnected;
	}

	/* Restore previous settings */
	intel_de_write(display, BCLRPAT(display, cpu_transcoder),
		       save_bclrpat);

	return status;
}

static int intel_spurious_crt_detect_dmi_callback(const struct dmi_system_id *id)
{
	DRM_DEBUG_DRIVER("Skipping CRT detection for %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id intel_spurious_crt_detect[] = {
	{
		.callback = intel_spurious_crt_detect_dmi_callback,
		.ident = "ACER ZGB",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ACER"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZGB"),
		},
	},
	{
		.callback = intel_spurious_crt_detect_dmi_callback,
		.ident = "Intel DZ77BH-55K",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "DZ77BH-55K"),
		},
	},
	{ }
};

static int
intel_crt_detect(struct drm_connector *connector,
		 struct drm_modeset_acquire_ctx *ctx,
		 bool force)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_crt *crt = intel_attached_crt(to_intel_connector(connector));
	struct intel_encoder *encoder = &crt->base;
	struct drm_atomic_state *state;
	intel_wakeref_t wakeref;
	int status;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] force=%d\n",
		    connector->base.id, connector->name,
		    force);

	if (!intel_display_device_enabled(display))
		return connector_status_disconnected;

	if (!intel_display_driver_check_access(display))
		return connector->status;

	if (display->params.load_detect_test) {
		wakeref = intel_display_power_get(display, encoder->power_domain);
		goto load_detect;
	}

	/* Skip machines without VGA that falsely report hotplug events */
	if (dmi_check_system(intel_spurious_crt_detect))
		return connector_status_disconnected;

	wakeref = intel_display_power_get(display, encoder->power_domain);

	if (HAS_HOTPLUG(display)) {
		/* We can not rely on the HPD pin always being correctly wired
		 * up, for example many KVM do not pass it through, and so
		 * only trust an assertion that the monitor is connected.
		 */
		if (intel_crt_detect_hotplug(connector)) {
			drm_dbg_kms(display->drm,
				    "CRT detected via hotplug\n");
			status = connector_status_connected;
			goto out;
		} else
			drm_dbg_kms(display->drm,
				    "CRT not detected via hotplug\n");
	}

	if (intel_crt_detect_ddc(connector)) {
		status = connector_status_connected;
		goto out;
	}

	/* Load detection is broken on HPD capable machines. Whoever wants a
	 * broken monitor (without edid) to work behind a broken kvm (that fails
	 * to have the right resistors for HP detection) needs to fix this up.
	 * For now just bail out. */
	if (HAS_HOTPLUG(display)) {
		status = connector_status_disconnected;
		goto out;
	}

load_detect:
	if (!force) {
		status = connector->status;
		goto out;
	}

	/* for pre-945g platforms use load detect */
	state = intel_load_detect_get_pipe(connector, ctx);
	if (IS_ERR(state)) {
		status = PTR_ERR(state);
	} else if (!state) {
		status = connector_status_unknown;
	} else {
		if (intel_crt_detect_ddc(connector))
			status = connector_status_connected;
		else if (DISPLAY_VER(display) < 4)
			status = intel_crt_load_detect(crt,
				to_intel_crtc(connector->state->crtc)->pipe);
		else if (display->params.load_detect_test)
			status = connector_status_disconnected;
		else
			status = connector_status_unknown;
		intel_load_detect_release_pipe(connector, state, ctx);
	}

out:
	intel_display_power_put(display, encoder->power_domain, wakeref);

	return status;
}

static int intel_crt_get_modes(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_crt *crt = intel_attached_crt(to_intel_connector(connector));
	struct intel_encoder *encoder = &crt->base;
	intel_wakeref_t wakeref;
	struct i2c_adapter *ddc;
	int ret;

	if (!intel_display_driver_check_access(display))
		return drm_edid_connector_add_modes(connector);

	wakeref = intel_display_power_get(display, encoder->power_domain);

	ret = intel_crt_ddc_get_modes(connector, connector->ddc);
	if (ret || !display->platform.g4x)
		goto out;

	/* Try to probe digital port for output in DVI-I -> VGA mode. */
	ddc = intel_gmbus_get_adapter(display, GMBUS_PIN_DPB);
	ret = intel_crt_ddc_get_modes(connector, ddc);

out:
	intel_display_power_put(display, encoder->power_domain, wakeref);

	return ret;
}

void intel_crt_reset(struct drm_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder->dev);
	struct intel_crt *crt = intel_encoder_to_crt(to_intel_encoder(encoder));

	if (DISPLAY_VER(display) >= 5) {
		u32 adpa;

		adpa = intel_de_read(display, crt->adpa_reg);
		adpa &= ~ADPA_HOTPLUG_MASK;
		adpa |= ADPA_HOTPLUG_BITS;
		intel_de_write(display, crt->adpa_reg, adpa);
		intel_de_posting_read(display, crt->adpa_reg);

		drm_dbg_kms(display->drm, "crt adpa set to 0x%x\n", adpa);
		crt->force_hotplug_required = true;
	}

}

/*
 * Routines for controlling stuff on the analog port
 */

static const struct drm_connector_funcs intel_crt_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs intel_crt_connector_helper_funcs = {
	.detect_ctx = intel_crt_detect,
	.mode_valid = intel_crt_mode_valid,
	.get_modes = intel_crt_get_modes,
};

static const struct drm_encoder_funcs intel_crt_enc_funcs = {
	.reset = intel_crt_reset,
	.destroy = intel_encoder_destroy,
};

void intel_crt_init(struct intel_display *display)
{
	struct intel_connector *connector;
	struct intel_crt *crt;
	i915_reg_t adpa_reg;
	u8 ddc_pin;
	u32 adpa;

	if (HAS_PCH_SPLIT(display))
		adpa_reg = PCH_ADPA;
	else if (display->platform.valleyview)
		adpa_reg = VLV_ADPA;
	else
		adpa_reg = ADPA;

	adpa = intel_de_read(display, adpa_reg);
	if ((adpa & ADPA_DAC_ENABLE) == 0) {
		/*
		 * On some machines (some IVB at least) CRT can be
		 * fused off, but there's no known fuse bit to
		 * indicate that. On these machine the ADPA register
		 * works normally, except the DAC enable bit won't
		 * take. So the only way to tell is attempt to enable
		 * it and see what happens.
		 */
		intel_de_write(display, adpa_reg,
			       adpa | ADPA_DAC_ENABLE |
			       ADPA_HSYNC_CNTL_DISABLE |
			       ADPA_VSYNC_CNTL_DISABLE);
		if ((intel_de_read(display, adpa_reg) & ADPA_DAC_ENABLE) == 0)
			return;
		intel_de_write(display, adpa_reg, adpa);
	}

	crt = kzalloc(sizeof(struct intel_crt), GFP_KERNEL);
	if (!crt)
		return;

	connector = intel_connector_alloc();
	if (!connector) {
		kfree(crt);
		return;
	}

	ddc_pin = display->vbt.crt_ddc_pin;

	drm_connector_init_with_ddc(display->drm, &connector->base,
				    &intel_crt_connector_funcs,
				    DRM_MODE_CONNECTOR_VGA,
				    intel_gmbus_get_adapter(display, ddc_pin));

	drm_encoder_init(display->drm, &crt->base.base, &intel_crt_enc_funcs,
			 DRM_MODE_ENCODER_DAC, "CRT");

	intel_connector_attach_encoder(connector, &crt->base);

	crt->base.type = INTEL_OUTPUT_ANALOG;
	crt->base.cloneable = BIT(INTEL_OUTPUT_DVO) | BIT(INTEL_OUTPUT_HDMI);
	if (display->platform.i830)
		crt->base.pipe_mask = BIT(PIPE_A);
	else
		crt->base.pipe_mask = ~0;

	if (DISPLAY_VER(display) != 2)
		connector->base.interlace_allowed = true;

	crt->adpa_reg = adpa_reg;

	crt->base.power_domain = POWER_DOMAIN_PORT_CRT;

	if (HAS_HOTPLUG(display) &&
	    !dmi_check_system(intel_spurious_crt_detect)) {
		crt->base.hpd_pin = HPD_CRT;
		crt->base.hotplug = intel_encoder_hotplug;
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	} else {
		connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	}
	connector->base.polled = connector->polled;

	if (HAS_DDI(display)) {
		assert_port_valid(display, PORT_E);

		crt->base.port = PORT_E;
		crt->base.get_config = hsw_crt_get_config;
		crt->base.get_hw_state = intel_ddi_get_hw_state;
		crt->base.compute_config = hsw_crt_compute_config;
		crt->base.pre_pll_enable = hsw_pre_pll_enable_crt;
		crt->base.pre_enable = hsw_pre_enable_crt;
		crt->base.enable = hsw_enable_crt;
		crt->base.disable = hsw_disable_crt;
		crt->base.post_disable = hsw_post_disable_crt;
		crt->base.enable_clock = hsw_ddi_enable_clock;
		crt->base.disable_clock = hsw_ddi_disable_clock;
		crt->base.is_clock_enabled = hsw_ddi_is_clock_enabled;

		intel_ddi_buf_trans_init(&crt->base);
	} else {
		if (HAS_PCH_SPLIT(display)) {
			crt->base.compute_config = pch_crt_compute_config;
			crt->base.disable = pch_disable_crt;
			crt->base.post_disable = pch_post_disable_crt;
		} else {
			crt->base.compute_config = intel_crt_compute_config;
			crt->base.disable = intel_disable_crt;
		}
		crt->base.port = PORT_NONE;
		crt->base.get_config = intel_crt_get_config;
		crt->base.get_hw_state = intel_crt_get_hw_state;
		crt->base.enable = intel_enable_crt;
	}
	connector->get_hw_state = intel_connector_get_hw_state;

	drm_connector_helper_add(&connector->base, &intel_crt_connector_helper_funcs);

	/*
	 * TODO: find a proper way to discover whether we need to set the the
	 * polarity and link reversal bits or not, instead of relying on the
	 * BIOS.
	 */
	if (HAS_PCH_LPT(display)) {
		u32 fdi_config = FDI_RX_POLARITY_REVERSED_LPT |
				 FDI_RX_LINK_REVERSAL_OVERRIDE;

		display->fdi.rx_config = intel_de_read(display,
						       FDI_RX_CTL(PIPE_A)) & fdi_config;
	}

	intel_crt_reset(&crt->base.base);
}
