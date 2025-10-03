// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_backlight_regs.h"
#include "intel_combo_phy.h"
#include "intel_combo_phy_regs.h"
#include "intel_crt.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_power_well.h"
#include "intel_display_regs.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_dkl_phy.h"
#include "intel_dkl_phy_regs.h"
#include "intel_dmc.h"
#include "intel_dmc_wl.h"
#include "intel_dp_aux_regs.h"
#include "intel_dpio_phy.h"
#include "intel_dpll.h"
#include "intel_hotplug.h"
#include "intel_pcode.h"
#include "intel_pps.h"
#include "intel_psr.h"
#include "intel_tc.h"
#include "intel_vga.h"
#include "skl_watermark.h"
#include "vlv_dpio_phy_regs.h"
#include "vlv_iosf_sb_reg.h"
#include "vlv_sideband.h"

/*
 * PG0 is HW controlled, so doesn't have a corresponding power well control knob
 *
 * {ICL,SKL}_DISP_PW1_IDX..{ICL,SKL}_DISP_PW4_IDX -> PG1..PG4
 */
static enum skl_power_gate pw_idx_to_pg(struct intel_display *display, int pw_idx)
{
	int pw1_idx = DISPLAY_VER(display) >= 11 ? ICL_PW_CTL_IDX_PW_1 : SKL_PW_CTL_IDX_PW_1;

	return pw_idx - pw1_idx + SKL_PG1;
}

struct i915_power_well_regs {
	i915_reg_t bios;
	i915_reg_t driver;
	i915_reg_t kvmr;
	i915_reg_t debug;
};

struct i915_power_well_ops {
	const struct i915_power_well_regs *regs;
	/*
	 * Synchronize the well's hw state to match the current sw state, for
	 * example enable/disable it based on the current refcount. Called
	 * during driver init and resume time, possibly after first calling
	 * the enable/disable handlers.
	 */
	void (*sync_hw)(struct intel_display *display,
			struct i915_power_well *power_well);
	/*
	 * Enable the well and resources that depend on it (for example
	 * interrupts located on the well). Called after the 0->1 refcount
	 * transition.
	 */
	void (*enable)(struct intel_display *display,
		       struct i915_power_well *power_well);
	/*
	 * Disable the well and resources that depend on it. Called after
	 * the 1->0 refcount transition.
	 */
	void (*disable)(struct intel_display *display,
			struct i915_power_well *power_well);
	/* Returns the hw enabled state. */
	bool (*is_enabled)(struct intel_display *display,
			   struct i915_power_well *power_well);
};

static const struct i915_power_well_instance *
i915_power_well_instance(const struct i915_power_well *power_well)
{
	return &power_well->desc->instances->list[power_well->instance_idx];
}

struct i915_power_well *
lookup_power_well(struct intel_display *display,
		  enum i915_power_well_id power_well_id)
{
	struct i915_power_well *power_well;

	for_each_power_well(display, power_well)
		if (i915_power_well_instance(power_well)->id == power_well_id)
			return power_well;

	/*
	 * It's not feasible to add error checking code to the callers since
	 * this condition really shouldn't happen and it doesn't even make sense
	 * to abort things like display initialization sequences. Just return
	 * the first power well and hope the WARN gets reported so we can fix
	 * our driver.
	 */
	drm_WARN(display->drm, 1,
		 "Power well %d not defined for this platform\n",
		 power_well_id);
	return &display->power.domains.power_wells[0];
}

void intel_power_well_enable(struct intel_display *display,
			     struct i915_power_well *power_well)
{
	drm_dbg_kms(display->drm, "enabling %s\n", intel_power_well_name(power_well));
	power_well->desc->ops->enable(display, power_well);
	power_well->hw_enabled = true;
}

void intel_power_well_disable(struct intel_display *display,
			      struct i915_power_well *power_well)
{
	drm_dbg_kms(display->drm, "disabling %s\n", intel_power_well_name(power_well));
	power_well->hw_enabled = false;
	power_well->desc->ops->disable(display, power_well);
}

void intel_power_well_sync_hw(struct intel_display *display,
			      struct i915_power_well *power_well)
{
	power_well->desc->ops->sync_hw(display, power_well);
	power_well->hw_enabled = power_well->desc->ops->is_enabled(display, power_well);
}

void intel_power_well_get(struct intel_display *display,
			  struct i915_power_well *power_well)
{
	if (!power_well->count++)
		intel_power_well_enable(display, power_well);
}

void intel_power_well_put(struct intel_display *display,
			  struct i915_power_well *power_well)
{
	drm_WARN(display->drm, !power_well->count,
		 "Use count on power well %s is already zero",
		 i915_power_well_instance(power_well)->name);

	if (!--power_well->count)
		intel_power_well_disable(display, power_well);
}

bool intel_power_well_is_enabled(struct intel_display *display,
				 struct i915_power_well *power_well)
{
	return power_well->desc->ops->is_enabled(display, power_well);
}

bool intel_power_well_is_enabled_cached(struct i915_power_well *power_well)
{
	return power_well->hw_enabled;
}

bool intel_display_power_well_is_enabled(struct intel_display *display,
					 enum i915_power_well_id power_well_id)
{
	struct i915_power_well *power_well;

	power_well = lookup_power_well(display, power_well_id);

	return intel_power_well_is_enabled(display, power_well);
}

bool intel_power_well_is_always_on(struct i915_power_well *power_well)
{
	return power_well->desc->always_on;
}

const char *intel_power_well_name(struct i915_power_well *power_well)
{
	return i915_power_well_instance(power_well)->name;
}

struct intel_power_domain_mask *intel_power_well_domains(struct i915_power_well *power_well)
{
	return &power_well->domains;
}

int intel_power_well_refcount(struct i915_power_well *power_well)
{
	return power_well->count;
}

/*
 * Starting with Haswell, we have a "Power Down Well" that can be turned off
 * when not needed anymore. We have 4 registers that can request the power well
 * to be enabled, and it will only be disabled if none of the registers is
 * requesting it to be enabled.
 */
static void hsw_power_well_post_enable(struct intel_display *display,
				       u8 irq_pipe_mask, bool has_vga)
{
	if (has_vga)
		intel_vga_reset_io_mem(display);

	if (irq_pipe_mask)
		gen8_irq_power_well_post_enable(display, irq_pipe_mask);
}

static void hsw_power_well_pre_disable(struct intel_display *display,
				       u8 irq_pipe_mask)
{
	if (irq_pipe_mask)
		gen8_irq_power_well_pre_disable(display, irq_pipe_mask);
}

#define ICL_AUX_PW_TO_PHY(pw_idx)	\
	((pw_idx) - ICL_PW_CTL_IDX_AUX_A + PHY_A)

#define ICL_AUX_PW_TO_CH(pw_idx)	\
	((pw_idx) - ICL_PW_CTL_IDX_AUX_A + AUX_CH_A)

#define ICL_TBT_AUX_PW_TO_CH(pw_idx)	\
	((pw_idx) - ICL_PW_CTL_IDX_AUX_TBT1 + AUX_CH_C)

static enum aux_ch icl_aux_pw_to_ch(const struct i915_power_well *power_well)
{
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;

	return power_well->desc->is_tc_tbt ? ICL_TBT_AUX_PW_TO_CH(pw_idx) :
					     ICL_AUX_PW_TO_CH(pw_idx);
}

static struct intel_digital_port *
aux_ch_to_digital_port(struct intel_display *display,
		       enum aux_ch aux_ch)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(display->drm, encoder) {
		struct intel_digital_port *dig_port;

		/* We'll check the MST primary port */
		if (encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		dig_port = enc_to_dig_port(encoder);

		if (dig_port && dig_port->aux_ch == aux_ch)
			return dig_port;
	}

	return NULL;
}

static enum phy icl_aux_pw_to_phy(struct intel_display *display,
				  const struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = icl_aux_pw_to_ch(power_well);
	struct intel_digital_port *dig_port = aux_ch_to_digital_port(display, aux_ch);

	/*
	 * FIXME should we care about the (VBT defined) dig_port->aux_ch
	 * relationship or should this be purely defined by the hardware layout?
	 * Currently if the port doesn't appear in the VBT, or if it's declared
	 * as HDMI-only and routed to a combo PHY, the encoder either won't be
	 * present at all or it will not have an aux_ch assigned.
	 */
	return dig_port ? intel_encoder_to_phy(&dig_port->base) : PHY_NONE;
}

static void hsw_wait_for_power_well_enable(struct intel_display *display,
					   struct i915_power_well *power_well,
					   bool timeout_expected)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;
	int timeout = power_well->desc->enable_timeout ? : 1;

	/*
	 * For some power wells we're not supposed to watch the status bit for
	 * an ack, but rather just wait a fixed amount of time and then
	 * proceed.  This is only used on DG2.
	 */
	if (display->platform.dg2 && power_well->desc->fixed_enable_delay) {
		usleep_range(600, 1200);
		return;
	}

	/* Timeout for PW1:10 us, AUX:not specified, other PWs:20 us. */
	if (intel_de_wait_for_set(display, regs->driver,
				  HSW_PWR_WELL_CTL_STATE(pw_idx), timeout)) {
		drm_dbg_kms(display->drm, "%s power well enable timeout\n",
			    intel_power_well_name(power_well));

		drm_WARN_ON(display->drm, !timeout_expected);

	}
}

static u32 hsw_power_well_requesters(struct intel_display *display,
				     const struct i915_power_well_regs *regs,
				     int pw_idx)
{
	u32 req_mask = HSW_PWR_WELL_CTL_REQ(pw_idx);
	u32 ret;

	ret = intel_de_read(display, regs->bios) & req_mask ? 1 : 0;
	ret |= intel_de_read(display, regs->driver) & req_mask ? 2 : 0;
	if (regs->kvmr.reg)
		ret |= intel_de_read(display, regs->kvmr) & req_mask ? 4 : 0;
	ret |= intel_de_read(display, regs->debug) & req_mask ? 8 : 0;

	return ret;
}

static void hsw_wait_for_power_well_disable(struct intel_display *display,
					    struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;
	u32 reqs;
	int ret;

	/*
	 * Bspec doesn't require waiting for PWs to get disabled, but still do
	 * this for paranoia. The known cases where a PW will be forced on:
	 * - a KVMR request on any power well via the KVMR request register
	 * - a DMC request on PW1 and MISC_IO power wells via the BIOS and
	 *   DEBUG request registers
	 * Skip the wait in case any of the request bits are set and print a
	 * diagnostic message.
	 */
	reqs = hsw_power_well_requesters(display, regs, pw_idx);

	ret = intel_de_wait_for_clear(display, regs->driver,
				      HSW_PWR_WELL_CTL_STATE(pw_idx),
				      reqs ? 0 : 1);
	if (!ret)
		return;

	/* Refresh requesters in case they popped up during the wait. */
	if (!reqs)
		reqs = hsw_power_well_requesters(display, regs, pw_idx);

	drm_dbg_kms(display->drm,
		    "%s forced on (bios:%d driver:%d kvmr:%d debug:%d)\n",
		    intel_power_well_name(power_well),
		    !!(reqs & 1), !!(reqs & 2), !!(reqs & 4), !!(reqs & 8));
}

static void gen9_wait_for_power_well_fuses(struct intel_display *display,
					   enum skl_power_gate pg)
{
	/* Timeout 5us for PG#0, for other PGs 1us */
	drm_WARN_ON(display->drm,
		    intel_de_wait_for_set(display, SKL_FUSE_STATUS,
					  SKL_FUSE_PG_DIST_STATUS(pg), 1));
}

static void hsw_power_well_enable(struct intel_display *display,
				  struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;

	if (power_well->desc->has_fuses) {
		enum skl_power_gate pg;

		pg = pw_idx_to_pg(display, pw_idx);

		/* Wa_16013190616:adlp */
		if (display->platform.alderlake_p && pg == SKL_PG1)
			intel_de_rmw(display, GEN8_CHICKEN_DCPR_1, 0, DISABLE_FLR_SRC);

		/*
		 * For PW1 we have to wait both for the PW0/PG0 fuse state
		 * before enabling the power well and PW1/PG1's own fuse
		 * state after the enabling. For all other power wells with
		 * fuses we only have to wait for that PW/PG's fuse state
		 * after the enabling.
		 */
		if (pg == SKL_PG1)
			gen9_wait_for_power_well_fuses(display, SKL_PG0);
	}

	intel_de_rmw(display, regs->driver, 0, HSW_PWR_WELL_CTL_REQ(pw_idx));

	hsw_wait_for_power_well_enable(display, power_well, false);

	if (power_well->desc->has_fuses) {
		enum skl_power_gate pg;

		pg = pw_idx_to_pg(display, pw_idx);

		gen9_wait_for_power_well_fuses(display, pg);
	}

	hsw_power_well_post_enable(display,
				   power_well->desc->irq_pipe_mask,
				   power_well->desc->has_vga);
}

static void hsw_power_well_disable(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;

	hsw_power_well_pre_disable(display,
				   power_well->desc->irq_pipe_mask);

	intel_de_rmw(display, regs->driver, HSW_PWR_WELL_CTL_REQ(pw_idx), 0);
	hsw_wait_for_power_well_disable(display, power_well);
}

static bool intel_aux_ch_is_edp(struct intel_display *display, enum aux_ch aux_ch)
{
	struct intel_digital_port *dig_port = aux_ch_to_digital_port(display, aux_ch);

	return dig_port && dig_port->base.type == INTEL_OUTPUT_EDP;
}

static void
icl_combo_phy_aux_power_well_enable(struct intel_display *display,
				    struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;

	drm_WARN_ON(display->drm, !display->platform.icelake);

	intel_de_rmw(display, regs->driver, 0, HSW_PWR_WELL_CTL_REQ(pw_idx));

	/*
	 * FIXME not sure if we should derive the PHY from the pw_idx, or
	 * from the VBT defined AUX_CH->DDI->PHY mapping.
	 */
	intel_de_rmw(display, ICL_PORT_CL_DW12(ICL_AUX_PW_TO_PHY(pw_idx)),
		     0, ICL_LANE_ENABLE_AUX);

	hsw_wait_for_power_well_enable(display, power_well, false);

	/* Display WA #1178: icl */
	if (pw_idx >= ICL_PW_CTL_IDX_AUX_A && pw_idx <= ICL_PW_CTL_IDX_AUX_B &&
	    !intel_aux_ch_is_edp(display, ICL_AUX_PW_TO_CH(pw_idx)))
		intel_de_rmw(display, ICL_PORT_TX_DW6_AUX(ICL_AUX_PW_TO_PHY(pw_idx)),
			     0, O_FUNC_OVRD_EN | O_LDO_BYPASS_CRI);
}

static void
icl_combo_phy_aux_power_well_disable(struct intel_display *display,
				     struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;

	drm_WARN_ON(display->drm, !display->platform.icelake);

	/*
	 * FIXME not sure if we should derive the PHY from the pw_idx, or
	 * from the VBT defined AUX_CH->DDI->PHY mapping.
	 */
	intel_de_rmw(display, ICL_PORT_CL_DW12(ICL_AUX_PW_TO_PHY(pw_idx)),
		     ICL_LANE_ENABLE_AUX, 0);

	intel_de_rmw(display, regs->driver, HSW_PWR_WELL_CTL_REQ(pw_idx), 0);

	hsw_wait_for_power_well_disable(display, power_well);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)

static void icl_tc_port_assert_ref_held(struct intel_display *display,
					struct i915_power_well *power_well,
					struct intel_digital_port *dig_port)
{
	if (drm_WARN_ON(display->drm, !dig_port))
		return;

	if (DISPLAY_VER(display) == 11 && intel_tc_cold_requires_aux_pw(dig_port))
		return;

	drm_WARN_ON(display->drm, !intel_tc_port_ref_held(dig_port));
}

#else

static void icl_tc_port_assert_ref_held(struct intel_display *display,
					struct i915_power_well *power_well,
					struct intel_digital_port *dig_port)
{
}

#endif

#define TGL_AUX_PW_TO_TC_PORT(pw_idx)	((pw_idx) - TGL_PW_CTL_IDX_AUX_TC1)

static void icl_tc_cold_exit(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	int ret, tries = 0;

	while (1) {
		ret = intel_pcode_write(display->drm, ICL_PCODE_EXIT_TCCOLD, 0);
		if (ret != -EAGAIN || ++tries == 3)
			break;
		msleep(1);
	}

	/* Spec states that TC cold exit can take up to 1ms to complete */
	if (!ret)
		msleep(1);

	/* TODO: turn failure into a error as soon i915 CI updates ICL IFWI */
	drm_dbg_kms(&i915->drm, "TC cold block %s\n", ret ? "failed" :
		    "succeeded");
}

static void
icl_tc_phy_aux_power_well_enable(struct intel_display *display,
				 struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = icl_aux_pw_to_ch(power_well);
	struct intel_digital_port *dig_port = aux_ch_to_digital_port(display, aux_ch);
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	bool is_tbt = power_well->desc->is_tc_tbt;
	bool timeout_expected;

	icl_tc_port_assert_ref_held(display, power_well, dig_port);

	intel_de_rmw(display, DP_AUX_CH_CTL(aux_ch),
		     DP_AUX_CH_CTL_TBT_IO, is_tbt ? DP_AUX_CH_CTL_TBT_IO : 0);

	intel_de_rmw(display, regs->driver,
		     0,
		     HSW_PWR_WELL_CTL_REQ(i915_power_well_instance(power_well)->hsw.idx));

	/*
	 * An AUX timeout is expected if the TBT DP tunnel is down,
	 * or need to enable AUX on a legacy TypeC port as part of the TC-cold
	 * exit sequence.
	 */
	timeout_expected = is_tbt || intel_tc_cold_requires_aux_pw(dig_port);
	if (DISPLAY_VER(display) == 11 && intel_tc_cold_requires_aux_pw(dig_port))
		icl_tc_cold_exit(display);

	hsw_wait_for_power_well_enable(display, power_well, timeout_expected);

	if (DISPLAY_VER(display) >= 12 && !is_tbt) {
		enum tc_port tc_port;

		tc_port = TGL_AUX_PW_TO_TC_PORT(i915_power_well_instance(power_well)->hsw.idx);

		if (wait_for(intel_dkl_phy_read(display, DKL_CMN_UC_DW_27(tc_port)) &
			     DKL_CMN_UC_DW27_UC_HEALTH, 1))
			drm_warn(display->drm,
				 "Timeout waiting TC uC health\n");
	}
}

static void
icl_aux_power_well_enable(struct intel_display *display,
			  struct i915_power_well *power_well)
{
	enum phy phy = icl_aux_pw_to_phy(display, power_well);

	if (intel_phy_is_tc(display, phy))
		return icl_tc_phy_aux_power_well_enable(display, power_well);
	else if (display->platform.icelake)
		return icl_combo_phy_aux_power_well_enable(display,
							   power_well);
	else
		return hsw_power_well_enable(display, power_well);
}

static void
icl_aux_power_well_disable(struct intel_display *display,
			   struct i915_power_well *power_well)
{
	enum phy phy = icl_aux_pw_to_phy(display, power_well);

	if (intel_phy_is_tc(display, phy))
		return hsw_power_well_disable(display, power_well);
	else if (display->platform.icelake)
		return icl_combo_phy_aux_power_well_disable(display,
							    power_well);
	else
		return hsw_power_well_disable(display, power_well);
}

/*
 * We should only use the power well if we explicitly asked the hardware to
 * enable it, so check if it's enabled and also check if we've requested it to
 * be enabled.
 */
static bool hsw_power_well_enabled(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	enum i915_power_well_id id = i915_power_well_instance(power_well)->id;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;
	u32 mask = HSW_PWR_WELL_CTL_REQ(pw_idx) |
		   HSW_PWR_WELL_CTL_STATE(pw_idx);
	u32 val;

	val = intel_de_read(display, regs->driver);

	/*
	 * On GEN9 big core due to a DMC bug the driver's request bits for PW1
	 * and the MISC_IO PW will be not restored, so check instead for the
	 * BIOS's own request bits, which are forced-on for these power wells
	 * when exiting DC5/6.
	 */
	if (DISPLAY_VER(display) == 9 && !display->platform.broxton &&
	    (id == SKL_DISP_PW_1 || id == SKL_DISP_PW_MISC_IO))
		val |= intel_de_read(display, regs->bios);

	return (val & mask) == mask;
}

static void assert_can_enable_dc9(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	drm_WARN_ONCE(display->drm,
		      (intel_de_read(display, DC_STATE_EN) & DC_STATE_EN_DC9),
		      "DC9 already programmed to be enabled.\n");
	drm_WARN_ONCE(display->drm,
		      intel_de_read(display, DC_STATE_EN) &
		      DC_STATE_EN_UPTO_DC5,
		      "DC5 still not disabled to enable DC9.\n");
	drm_WARN_ONCE(display->drm,
		      intel_de_read(display, HSW_PWR_WELL_CTL2) &
		      HSW_PWR_WELL_CTL_REQ(SKL_PW_CTL_IDX_PW_2),
		      "Power well 2 on.\n");
	drm_WARN_ONCE(display->drm, intel_irqs_enabled(dev_priv),
		      "Interrupts not disabled yet.\n");

	 /*
	  * TODO: check for the following to verify the conditions to enter DC9
	  * state are satisfied:
	  * 1] Check relevant display engine registers to verify if mode set
	  * disable sequence was followed.
	  * 2] Check if display uninitialize sequence is initialized.
	  */
}

static void assert_can_disable_dc9(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	drm_WARN_ONCE(display->drm, intel_irqs_enabled(dev_priv),
		      "Interrupts not disabled yet.\n");
	drm_WARN_ONCE(display->drm,
		      intel_de_read(display, DC_STATE_EN) &
		      DC_STATE_EN_UPTO_DC5,
		      "DC5 still not disabled.\n");

	 /*
	  * TODO: check for the following to verify DC9 state was indeed
	  * entered before programming to disable it:
	  * 1] Check relevant display engine registers to verify if mode
	  *  set disable sequence was followed.
	  * 2] Check if display uninitialize sequence is initialized.
	  */
}

static void gen9_write_dc_state(struct intel_display *display,
				u32 state)
{
	int rewrites = 0;
	int rereads = 0;
	u32 v;

	intel_de_write(display, DC_STATE_EN, state);

	/* It has been observed that disabling the dc6 state sometimes
	 * doesn't stick and dmc keeps returning old value. Make sure
	 * the write really sticks enough times and also force rewrite until
	 * we are confident that state is exactly what we want.
	 */
	do  {
		v = intel_de_read(display, DC_STATE_EN);

		if (v != state) {
			intel_de_write(display, DC_STATE_EN, state);
			rewrites++;
			rereads = 0;
		} else if (rereads++ > 5) {
			break;
		}

	} while (rewrites < 100);

	if (v != state)
		drm_err(display->drm,
			"Writing dc state to 0x%x failed, now 0x%x\n",
			state, v);

	/* Most of the times we need one retry, avoid spam */
	if (rewrites > 1)
		drm_dbg_kms(display->drm,
			    "Rewrote dc state to 0x%x %d times\n",
			    state, rewrites);
}

static u32 gen9_dc_mask(struct intel_display *display)
{
	u32 mask;

	mask = DC_STATE_EN_UPTO_DC5;

	if (DISPLAY_VER(display) >= 12)
		mask |= DC_STATE_EN_DC3CO | DC_STATE_EN_UPTO_DC6
					  | DC_STATE_EN_DC9;
	else if (DISPLAY_VER(display) == 11)
		mask |= DC_STATE_EN_UPTO_DC6 | DC_STATE_EN_DC9;
	else if (display->platform.geminilake || display->platform.broxton)
		mask |= DC_STATE_EN_DC9;
	else
		mask |= DC_STATE_EN_UPTO_DC6;

	return mask;
}

void gen9_sanitize_dc_state(struct intel_display *display)
{
	struct i915_power_domains *power_domains = &display->power.domains;
	u32 val;

	if (!HAS_DISPLAY(display))
		return;

	val = intel_de_read(display, DC_STATE_EN) & gen9_dc_mask(display);

	drm_dbg_kms(display->drm,
		    "Resetting DC state tracking from %02x to %02x\n",
		    power_domains->dc_state, val);
	power_domains->dc_state = val;
}

/**
 * gen9_set_dc_state - set target display C power state
 * @display: display instance
 * @state: target DC power state
 * - DC_STATE_DISABLE
 * - DC_STATE_EN_UPTO_DC5
 * - DC_STATE_EN_UPTO_DC6
 * - DC_STATE_EN_DC9
 *
 * Signal to DMC firmware/HW the target DC power state passed in @state.
 * DMC/HW can turn off individual display clocks and power rails when entering
 * a deeper DC power state (higher in number) and turns these back when exiting
 * that state to a shallower power state (lower in number). The HW will decide
 * when to actually enter a given state on an on-demand basis, for instance
 * depending on the active state of display pipes. The state of display
 * registers backed by affected power rails are saved/restored as needed.
 *
 * Based on the above enabling a deeper DC power state is asynchronous wrt.
 * enabling it. Disabling a deeper power state is synchronous: for instance
 * setting %DC_STATE_DISABLE won't complete until all HW resources are turned
 * back on and register state is restored. This is guaranteed by the MMIO write
 * to DC_STATE_EN blocking until the state is restored.
 */
void gen9_set_dc_state(struct intel_display *display, u32 state)
{
	struct i915_power_domains *power_domains = &display->power.domains;
	bool dc6_was_enabled, enable_dc6;
	u32 mask;
	u32 val;

	if (!HAS_DISPLAY(display))
		return;

	if (drm_WARN_ON_ONCE(display->drm,
			     state & ~power_domains->allowed_dc_mask))
		state &= power_domains->allowed_dc_mask;

	if (!power_domains->initializing)
		intel_psr_notify_dc5_dc6(display);

	val = intel_de_read(display, DC_STATE_EN);
	mask = gen9_dc_mask(display);
	drm_dbg_kms(display->drm, "Setting DC state from %02x to %02x\n",
		    val & mask, state);

	/* Check if DMC is ignoring our DC state requests */
	if ((val & mask) != power_domains->dc_state)
		drm_err(display->drm, "DC state mismatch (0x%x -> 0x%x)\n",
			power_domains->dc_state, val & mask);

	enable_dc6 = state & DC_STATE_EN_UPTO_DC6;
	dc6_was_enabled = val & DC_STATE_EN_UPTO_DC6;
	if (!dc6_was_enabled && enable_dc6)
		intel_dmc_update_dc6_allowed_count(display, true);

	val &= ~mask;
	val |= state;

	gen9_write_dc_state(display, val);

	if (!enable_dc6 && dc6_was_enabled)
		intel_dmc_update_dc6_allowed_count(display, false);

	power_domains->dc_state = val & mask;
}

static void tgl_enable_dc3co(struct intel_display *display)
{
	drm_dbg_kms(display->drm, "Enabling DC3CO\n");
	gen9_set_dc_state(display, DC_STATE_EN_DC3CO);
}

static void tgl_disable_dc3co(struct intel_display *display)
{
	drm_dbg_kms(display->drm, "Disabling DC3CO\n");
	intel_de_rmw(display, DC_STATE_EN, DC_STATE_DC3CO_STATUS, 0);
	gen9_set_dc_state(display, DC_STATE_DISABLE);
	/*
	 * Delay of 200us DC3CO Exit time B.Spec 49196
	 */
	usleep_range(200, 210);
}

static void assert_can_enable_dc5(struct intel_display *display)
{
	enum i915_power_well_id high_pg;

	/* Power wells at this level and above must be disabled for DC5 entry */
	if (DISPLAY_VER(display) == 12)
		high_pg = ICL_DISP_PW_3;
	else
		high_pg = SKL_DISP_PW_2;

	drm_WARN_ONCE(display->drm,
		      intel_display_power_well_is_enabled(display, high_pg),
		      "Power wells above platform's DC5 limit still enabled.\n");

	drm_WARN_ONCE(display->drm,
		      (intel_de_read(display, DC_STATE_EN) &
		       DC_STATE_EN_UPTO_DC5),
		      "DC5 already programmed to be enabled.\n");

	assert_display_rpm_held(display);

	assert_main_dmc_loaded(display);
}

void gen9_enable_dc5(struct intel_display *display)
{
	assert_can_enable_dc5(display);

	drm_dbg_kms(display->drm, "Enabling DC5\n");

	/* Wa Display #1183: skl,kbl,cfl */
	if (DISPLAY_VER(display) == 9 && !display->platform.broxton)
		intel_de_rmw(display, GEN8_CHICKEN_DCPR_1,
			     0, SKL_SELECT_ALTERNATE_DC_EXIT);

	intel_dmc_wl_enable(display, DC_STATE_EN_UPTO_DC5);

	gen9_set_dc_state(display, DC_STATE_EN_UPTO_DC5);
}

static void assert_can_enable_dc6(struct intel_display *display)
{
	drm_WARN_ONCE(display->drm,
		      (intel_de_read(display, UTIL_PIN_CTL) &
		       (UTIL_PIN_ENABLE | UTIL_PIN_MODE_MASK)) ==
		      (UTIL_PIN_ENABLE | UTIL_PIN_MODE_PWM),
		      "Utility pin enabled in PWM mode\n");
	drm_WARN_ONCE(display->drm,
		      (intel_de_read(display, DC_STATE_EN) &
		       DC_STATE_EN_UPTO_DC6),
		      "DC6 already programmed to be enabled.\n");

	assert_main_dmc_loaded(display);
}

void skl_enable_dc6(struct intel_display *display)
{
	assert_can_enable_dc6(display);

	drm_dbg_kms(display->drm, "Enabling DC6\n");

	/* Wa Display #1183: skl,kbl,cfl */
	if (DISPLAY_VER(display) == 9 && !display->platform.broxton)
		intel_de_rmw(display, GEN8_CHICKEN_DCPR_1,
			     0, SKL_SELECT_ALTERNATE_DC_EXIT);

	intel_dmc_wl_enable(display, DC_STATE_EN_UPTO_DC6);

	gen9_set_dc_state(display, DC_STATE_EN_UPTO_DC6);
}

void bxt_enable_dc9(struct intel_display *display)
{
	assert_can_enable_dc9(display);

	drm_dbg_kms(display->drm, "Enabling DC9\n");
	/*
	 * Power sequencer reset is needed on BXT/GLK, because the PPS registers
	 * aren't always on, unlike with South Display Engine on PCH.
	 */
	if (display->platform.broxton || display->platform.geminilake)
		bxt_pps_reset_all(display);
	gen9_set_dc_state(display, DC_STATE_EN_DC9);
}

void bxt_disable_dc9(struct intel_display *display)
{
	assert_can_disable_dc9(display);

	drm_dbg_kms(display->drm, "Disabling DC9\n");

	gen9_set_dc_state(display, DC_STATE_DISABLE);

	intel_pps_unlock_regs_wa(display);
}

static void hsw_power_well_sync_hw(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->ops->regs;
	int pw_idx = i915_power_well_instance(power_well)->hsw.idx;
	u32 mask = HSW_PWR_WELL_CTL_REQ(pw_idx);
	u32 bios_req = intel_de_read(display, regs->bios);

	/* Take over the request bit if set by BIOS. */
	if (bios_req & mask) {
		u32 drv_req = intel_de_read(display, regs->driver);

		if (!(drv_req & mask))
			intel_de_write(display, regs->driver, drv_req | mask);
		intel_de_write(display, regs->bios, bios_req & ~mask);
	}
}

static void bxt_dpio_cmn_power_well_enable(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	bxt_dpio_phy_init(display, i915_power_well_instance(power_well)->bxt.phy);
}

static void bxt_dpio_cmn_power_well_disable(struct intel_display *display,
					    struct i915_power_well *power_well)
{
	bxt_dpio_phy_uninit(display, i915_power_well_instance(power_well)->bxt.phy);
}

static bool bxt_dpio_cmn_power_well_enabled(struct intel_display *display,
					    struct i915_power_well *power_well)
{
	return bxt_dpio_phy_is_enabled(display, i915_power_well_instance(power_well)->bxt.phy);
}

static void bxt_verify_dpio_phy_power_wells(struct intel_display *display)
{
	struct i915_power_well *power_well;

	power_well = lookup_power_well(display, BXT_DISP_PW_DPIO_CMN_A);
	if (intel_power_well_refcount(power_well) > 0)
		bxt_dpio_phy_verify_state(display, i915_power_well_instance(power_well)->bxt.phy);

	power_well = lookup_power_well(display, VLV_DISP_PW_DPIO_CMN_BC);
	if (intel_power_well_refcount(power_well) > 0)
		bxt_dpio_phy_verify_state(display, i915_power_well_instance(power_well)->bxt.phy);

	if (display->platform.geminilake) {
		power_well = lookup_power_well(display,
					       GLK_DISP_PW_DPIO_CMN_C);
		if (intel_power_well_refcount(power_well) > 0)
			bxt_dpio_phy_verify_state(display,
						  i915_power_well_instance(power_well)->bxt.phy);
	}
}

static bool gen9_dc_off_power_well_enabled(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	return ((intel_de_read(display, DC_STATE_EN) & DC_STATE_EN_DC3CO) == 0 &&
		(intel_de_read(display, DC_STATE_EN) & DC_STATE_EN_UPTO_DC5_DC6_MASK) == 0);
}

static void gen9_assert_dbuf_enabled(struct intel_display *display)
{
	u8 hw_enabled_dbuf_slices = intel_enabled_dbuf_slices_mask(display);
	u8 enabled_dbuf_slices = display->dbuf.enabled_slices;

	drm_WARN(display->drm,
		 hw_enabled_dbuf_slices != enabled_dbuf_slices,
		 "Unexpected DBuf power power state (0x%08x, expected 0x%08x)\n",
		 hw_enabled_dbuf_slices,
		 enabled_dbuf_slices);
}

void gen9_disable_dc_states(struct intel_display *display)
{
	struct i915_power_domains *power_domains = &display->power.domains;
	struct intel_cdclk_config cdclk_config = {};
	u32 old_state = power_domains->dc_state;

	if (power_domains->target_dc_state == DC_STATE_EN_DC3CO) {
		tgl_disable_dc3co(display);
		return;
	}

	if (HAS_DISPLAY(display)) {
		intel_dmc_wl_get_noreg(display);
		gen9_set_dc_state(display, DC_STATE_DISABLE);
		intel_dmc_wl_put_noreg(display);
	} else {
		gen9_set_dc_state(display, DC_STATE_DISABLE);
		return;
	}

	if (old_state == DC_STATE_EN_UPTO_DC5 ||
	    old_state == DC_STATE_EN_UPTO_DC6)
		intel_dmc_wl_disable(display);

	intel_cdclk_get_cdclk(display, &cdclk_config);
	/* Can't read out voltage_level so can't use intel_cdclk_changed() */
	drm_WARN_ON(display->drm,
		    intel_cdclk_clock_changed(&display->cdclk.hw,
					      &cdclk_config));

	gen9_assert_dbuf_enabled(display);

	if (display->platform.geminilake || display->platform.broxton)
		bxt_verify_dpio_phy_power_wells(display);

	if (DISPLAY_VER(display) >= 11)
		/*
		 * DMC retains HW context only for port A, the other combo
		 * PHY's HW context for port B is lost after DC transitions,
		 * so we need to restore it manually.
		 */
		intel_combo_phy_init(display);
}

static void gen9_dc_off_power_well_enable(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	gen9_disable_dc_states(display);
}

static void gen9_dc_off_power_well_disable(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	struct i915_power_domains *power_domains = &display->power.domains;

	if (!intel_dmc_has_payload(display))
		return;

	switch (power_domains->target_dc_state) {
	case DC_STATE_EN_DC3CO:
		tgl_enable_dc3co(display);
		break;
	case DC_STATE_EN_UPTO_DC6:
		skl_enable_dc6(display);
		break;
	case DC_STATE_EN_UPTO_DC5:
		gen9_enable_dc5(display);
		break;
	}
}

static void i9xx_power_well_sync_hw_noop(struct intel_display *display,
					 struct i915_power_well *power_well)
{
}

static void i9xx_always_on_power_well_noop(struct intel_display *display,
					   struct i915_power_well *power_well)
{
}

static bool i9xx_always_on_power_well_enabled(struct intel_display *display,
					      struct i915_power_well *power_well)
{
	return true;
}

static void i830_pipes_power_well_enable(struct intel_display *display,
					 struct i915_power_well *power_well)
{
	if ((intel_de_read(display, TRANSCONF(display, PIPE_A)) & TRANSCONF_ENABLE) == 0)
		i830_enable_pipe(display, PIPE_A);
	if ((intel_de_read(display, TRANSCONF(display, PIPE_B)) & TRANSCONF_ENABLE) == 0)
		i830_enable_pipe(display, PIPE_B);
}

static void i830_pipes_power_well_disable(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	i830_disable_pipe(display, PIPE_B);
	i830_disable_pipe(display, PIPE_A);
}

static bool i830_pipes_power_well_enabled(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	return intel_de_read(display, TRANSCONF(display, PIPE_A)) & TRANSCONF_ENABLE &&
		intel_de_read(display, TRANSCONF(display, PIPE_B)) & TRANSCONF_ENABLE;
}

static void i830_pipes_power_well_sync_hw(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	if (intel_power_well_refcount(power_well) > 0)
		i830_pipes_power_well_enable(display, power_well);
	else
		i830_pipes_power_well_disable(display, power_well);
}

static void vlv_set_power_well(struct intel_display *display,
			       struct i915_power_well *power_well, bool enable)
{
	int pw_idx = i915_power_well_instance(power_well)->vlv.idx;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(pw_idx);
	state = enable ? PUNIT_PWRGT_PWR_ON(pw_idx) :
			 PUNIT_PWRGT_PWR_GATE(pw_idx);

	vlv_punit_get(display->drm);

#define COND \
	((vlv_punit_read(display->drm, PUNIT_REG_PWRGT_STATUS) & mask) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(display->drm, PUNIT_REG_PWRGT_CTRL);
	ctrl &= ~mask;
	ctrl |= state;
	vlv_punit_write(display->drm, PUNIT_REG_PWRGT_CTRL, ctrl);

	if (wait_for(COND, 100))
		drm_err(display->drm,
			"timeout setting power well state %08x (%08x)\n",
			state,
			vlv_punit_read(display->drm, PUNIT_REG_PWRGT_CTRL));

#undef COND

out:
	vlv_punit_put(display->drm);
}

static void vlv_power_well_enable(struct intel_display *display,
				  struct i915_power_well *power_well)
{
	vlv_set_power_well(display, power_well, true);
}

static void vlv_power_well_disable(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	vlv_set_power_well(display, power_well, false);
}

static bool vlv_power_well_enabled(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	int pw_idx = i915_power_well_instance(power_well)->vlv.idx;
	bool enabled = false;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(pw_idx);
	ctrl = PUNIT_PWRGT_PWR_ON(pw_idx);

	vlv_punit_get(display->drm);

	state = vlv_punit_read(display->drm, PUNIT_REG_PWRGT_STATUS) & mask;
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	drm_WARN_ON(display->drm, state != PUNIT_PWRGT_PWR_ON(pw_idx) &&
		    state != PUNIT_PWRGT_PWR_GATE(pw_idx));
	if (state == ctrl)
		enabled = true;

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(display->drm, PUNIT_REG_PWRGT_CTRL) & mask;
	drm_WARN_ON(display->drm, ctrl != state);

	vlv_punit_put(display->drm);

	return enabled;
}

static void vlv_init_display_clock_gating(struct intel_display *display)
{
	/*
	 * On driver load, a pipe may be active and driving a DSI display.
	 * Preserve DPOUNIT_CLOCK_GATE_DISABLE to avoid the pipe getting stuck
	 * (and never recovering) in this case. intel_dsi_post_disable() will
	 * clear it when we turn off the display.
	 */
	intel_de_rmw(display, DSPCLK_GATE_D(display),
		     ~DPOUNIT_CLOCK_GATE_DISABLE, VRHUNIT_CLOCK_GATE_DISABLE);

	/*
	 * Disable trickle feed and enable pnd deadline calculation
	 */
	intel_de_write(display, MI_ARB_VLV,
		       MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE);
	intel_de_write(display, CBR1_VLV, 0);

	drm_WARN_ON(display->drm, DISPLAY_RUNTIME_INFO(display)->rawclk_freq == 0);
	intel_de_write(display, RAWCLK_FREQ_VLV,
		       DIV_ROUND_CLOSEST(DISPLAY_RUNTIME_INFO(display)->rawclk_freq,
					 1000));
}

static void vlv_display_power_well_init(struct intel_display *display)
{
	struct intel_encoder *encoder;
	enum pipe pipe;

	/*
	 * Enable the CRI clock source so we can get at the
	 * display and the reference clock for VGA
	 * hotplug / manual detection. Supposedly DSI also
	 * needs the ref clock up and running.
	 *
	 * CHV DPLL B/C have some issues if VGA mode is enabled.
	 */
	for_each_pipe(display, pipe) {
		u32 val = intel_de_read(display, DPLL(display, pipe));

		val |= DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
		if (pipe != PIPE_A)
			val |= DPLL_INTEGRATED_CRI_CLK_VLV;

		intel_de_write(display, DPLL(display, pipe), val);
	}

	vlv_init_display_clock_gating(display);

	valleyview_enable_display_irqs(display);

	/*
	 * During driver initialization/resume we can avoid restoring the
	 * part of the HW/SW state that will be inited anyway explicitly.
	 */
	if (display->power.domains.initializing)
		return;

	intel_hpd_init(display);
	intel_hpd_poll_disable(display);

	/* Re-enable the ADPA, if we have one */
	for_each_intel_encoder(display->drm, encoder) {
		if (encoder->type == INTEL_OUTPUT_ANALOG)
			intel_crt_reset(&encoder->base);
	}

	intel_vga_disable(display);

	intel_pps_unlock_regs_wa(display);
}

static void vlv_display_power_well_deinit(struct intel_display *display)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	valleyview_disable_display_irqs(display);

	/* make sure we're done processing display irqs */
	intel_synchronize_irq(dev_priv);

	vlv_pps_reset_all(display);

	/* Prevent us from re-enabling polling on accident in late suspend */
	if (!display->drm->dev->power.is_suspended)
		intel_hpd_poll_enable(display);
}

static void vlv_display_power_well_enable(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	vlv_set_power_well(display, power_well, true);

	vlv_display_power_well_init(display);
}

static void vlv_display_power_well_disable(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	vlv_display_power_well_deinit(display);

	vlv_set_power_well(display, power_well, false);
}

static void vlv_dpio_cmn_power_well_enable(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	/* since ref/cri clock was enabled */
	udelay(1); /* >10ns for cmnreset, >0ns for sidereset */

	vlv_set_power_well(display, power_well, true);

	/*
	 * From VLV2A0_DP_eDP_DPIO_driver_vbios_notes_10.docx -
	 *  6.	De-assert cmn_reset/side_reset. Same as VLV X0.
	 *   a.	GUnit 0x2110 bit[0] set to 1 (def 0)
	 *   b.	The other bits such as sfr settings / modesel may all
	 *	be set to 0.
	 *
	 * This should only be done on init and resume from S3 with
	 * both PLLs disabled, or we risk losing DPIO and PLL
	 * synchronization.
	 */
	intel_de_rmw(display, DPIO_CTL, 0, DPIO_CMNRST);
}

static void vlv_dpio_cmn_power_well_disable(struct intel_display *display,
					    struct i915_power_well *power_well)
{
	enum pipe pipe;

	for_each_pipe(display, pipe)
		assert_pll_disabled(display, pipe);

	/* Assert common reset */
	intel_de_rmw(display, DPIO_CTL, DPIO_CMNRST, 0);

	vlv_set_power_well(display, power_well, false);
}

#define BITS_SET(val, bits) (((val) & (bits)) == (bits))

static void assert_chv_phy_status(struct intel_display *display)
{
	struct i915_power_well *cmn_bc =
		lookup_power_well(display, VLV_DISP_PW_DPIO_CMN_BC);
	struct i915_power_well *cmn_d =
		lookup_power_well(display, CHV_DISP_PW_DPIO_CMN_D);
	u32 phy_control = display->power.chv_phy_control;
	u32 phy_status = 0;
	u32 phy_status_mask = 0xffffffff;

	/*
	 * The BIOS can leave the PHY is some weird state
	 * where it doesn't fully power down some parts.
	 * Disable the asserts until the PHY has been fully
	 * reset (ie. the power well has been disabled at
	 * least once).
	 */
	if (!display->power.chv_phy_assert[DPIO_PHY0])
		phy_status_mask &= ~(PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 1) |
				     PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH1) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 1));

	if (!display->power.chv_phy_assert[DPIO_PHY1])
		phy_status_mask &= ~(PHY_STATUS_CMN_LDO(DPIO_PHY1, DPIO_CH0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 1));

	if (intel_power_well_is_enabled(display, cmn_bc)) {
		phy_status |= PHY_POWERGOOD(DPIO_PHY0);

		/* this assumes override is only used to enable lanes */
		if ((phy_control & PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY0, DPIO_CH0)) == 0)
			phy_control |= PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH0);

		if ((phy_control & PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY0, DPIO_CH1)) == 0)
			phy_control |= PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH1);

		/* CL1 is on whenever anything is on in either channel */
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH0) |
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH1)))
			phy_status |= PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH0);

		/*
		 * The DPLLB check accounts for the pipe B + port A usage
		 * with CL2 powered up but all the lanes in the second channel
		 * powered down.
		 */
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH1)) &&
		    (intel_de_read(display, DPLL(display, PIPE_B)) & DPLL_VCO_ENABLE) == 0)
			phy_status |= PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH1);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0x3, DPIO_PHY0, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 0);
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xc, DPIO_PHY0, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 1);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0x3, DPIO_PHY0, DPIO_CH1)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 0);
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xc, DPIO_PHY0, DPIO_CH1)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 1);
	}

	if (intel_power_well_is_enabled(display, cmn_d)) {
		phy_status |= PHY_POWERGOOD(DPIO_PHY1);

		/* this assumes override is only used to enable lanes */
		if ((phy_control & PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY1, DPIO_CH0)) == 0)
			phy_control |= PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY1, DPIO_CH0);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY1, DPIO_CH0)))
			phy_status |= PHY_STATUS_CMN_LDO(DPIO_PHY1, DPIO_CH0);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0x3, DPIO_PHY1, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 0);
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xc, DPIO_PHY1, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 1);
	}

	phy_status &= phy_status_mask;

	/*
	 * The PHY may be busy with some initial calibration and whatnot,
	 * so the power state can take a while to actually change.
	 */
	if (intel_de_wait(display, DISPLAY_PHY_STATUS,
			  phy_status_mask, phy_status, 10))
		drm_err(display->drm,
			"Unexpected PHY_STATUS 0x%08x, expected 0x%08x (PHY_CONTROL=0x%08x)\n",
			intel_de_read(display, DISPLAY_PHY_STATUS) & phy_status_mask,
			phy_status, display->power.chv_phy_control);
}

#undef BITS_SET

static void chv_dpio_cmn_power_well_enable(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	enum i915_power_well_id id = i915_power_well_instance(power_well)->id;
	enum dpio_phy phy;
	u32 tmp;

	drm_WARN_ON_ONCE(display->drm,
			 id != VLV_DISP_PW_DPIO_CMN_BC &&
			 id != CHV_DISP_PW_DPIO_CMN_D);

	if (id == VLV_DISP_PW_DPIO_CMN_BC)
		phy = DPIO_PHY0;
	else
		phy = DPIO_PHY1;

	/* since ref/cri clock was enabled */
	udelay(1); /* >10ns for cmnreset, >0ns for sidereset */
	vlv_set_power_well(display, power_well, true);

	/* Poll for phypwrgood signal */
	if (intel_de_wait_for_set(display, DISPLAY_PHY_STATUS,
				  PHY_POWERGOOD(phy), 1))
		drm_err(display->drm, "Display PHY %d is not power up\n",
			phy);

	vlv_dpio_get(display->drm);

	/* Enable dynamic power down */
	tmp = vlv_dpio_read(display->drm, phy, CHV_CMN_DW28);
	tmp |= DPIO_DYNPWRDOWNEN_CH0 | DPIO_CL1POWERDOWNEN |
		DPIO_SUS_CLK_CONFIG_GATE_CLKREQ;
	vlv_dpio_write(display->drm, phy, CHV_CMN_DW28, tmp);

	if (id == VLV_DISP_PW_DPIO_CMN_BC) {
		tmp = vlv_dpio_read(display->drm, phy, CHV_CMN_DW6_CH1);
		tmp |= DPIO_DYNPWRDOWNEN_CH1;
		vlv_dpio_write(display->drm, phy, CHV_CMN_DW6_CH1, tmp);
	} else {
		/*
		 * Force the non-existing CL2 off. BXT does this
		 * too, so maybe it saves some power even though
		 * CL2 doesn't exist?
		 */
		tmp = vlv_dpio_read(display->drm, phy, CHV_CMN_DW30);
		tmp |= DPIO_CL2_LDOFUSE_PWRENB;
		vlv_dpio_write(display->drm, phy, CHV_CMN_DW30, tmp);
	}

	vlv_dpio_put(display->drm);

	display->power.chv_phy_control |= PHY_COM_LANE_RESET_DEASSERT(phy);
	intel_de_write(display, DISPLAY_PHY_CONTROL,
		       display->power.chv_phy_control);

	drm_dbg_kms(display->drm,
		    "Enabled DPIO PHY%d (PHY_CONTROL=0x%08x)\n",
		    phy, display->power.chv_phy_control);

	assert_chv_phy_status(display);
}

static void chv_dpio_cmn_power_well_disable(struct intel_display *display,
					    struct i915_power_well *power_well)
{
	enum i915_power_well_id id = i915_power_well_instance(power_well)->id;
	enum dpio_phy phy;

	drm_WARN_ON_ONCE(display->drm,
			 id != VLV_DISP_PW_DPIO_CMN_BC &&
			 id != CHV_DISP_PW_DPIO_CMN_D);

	if (id == VLV_DISP_PW_DPIO_CMN_BC) {
		phy = DPIO_PHY0;
		assert_pll_disabled(display, PIPE_A);
		assert_pll_disabled(display, PIPE_B);
	} else {
		phy = DPIO_PHY1;
		assert_pll_disabled(display, PIPE_C);
	}

	display->power.chv_phy_control &= ~PHY_COM_LANE_RESET_DEASSERT(phy);
	intel_de_write(display, DISPLAY_PHY_CONTROL,
		       display->power.chv_phy_control);

	vlv_set_power_well(display, power_well, false);

	drm_dbg_kms(display->drm,
		    "Disabled DPIO PHY%d (PHY_CONTROL=0x%08x)\n",
		    phy, display->power.chv_phy_control);

	/* PHY is fully reset now, so we can enable the PHY state asserts */
	display->power.chv_phy_assert[phy] = true;

	assert_chv_phy_status(display);
}

static void assert_chv_phy_powergate(struct intel_display *display, enum dpio_phy phy,
				     enum dpio_channel ch, bool override, unsigned int mask)
{
	u32 reg, val, expected, actual;

	/*
	 * The BIOS can leave the PHY is some weird state
	 * where it doesn't fully power down some parts.
	 * Disable the asserts until the PHY has been fully
	 * reset (ie. the power well has been disabled at
	 * least once).
	 */
	if (!display->power.chv_phy_assert[phy])
		return;

	if (ch == DPIO_CH0)
		reg = CHV_CMN_DW0_CH0;
	else
		reg = CHV_CMN_DW6_CH1;

	vlv_dpio_get(display->drm);
	val = vlv_dpio_read(display->drm, phy, reg);
	vlv_dpio_put(display->drm);

	/*
	 * This assumes !override is only used when the port is disabled.
	 * All lanes should power down even without the override when
	 * the port is disabled.
	 */
	if (!override || mask == 0xf) {
		expected = DPIO_ALLDL_POWERDOWN | DPIO_ANYDL_POWERDOWN;
		/*
		 * If CH1 common lane is not active anymore
		 * (eg. for pipe B DPLL) the entire channel will
		 * shut down, which causes the common lane registers
		 * to read as 0. That means we can't actually check
		 * the lane power down status bits, but as the entire
		 * register reads as 0 it's a good indication that the
		 * channel is indeed entirely powered down.
		 */
		if (ch == DPIO_CH1 && val == 0)
			expected = 0;
	} else if (mask != 0x0) {
		expected = DPIO_ANYDL_POWERDOWN;
	} else {
		expected = 0;
	}

	if (ch == DPIO_CH0)
		actual = REG_FIELD_GET(DPIO_ANYDL_POWERDOWN_CH0 |
				       DPIO_ALLDL_POWERDOWN_CH0, val);
	else
		actual = REG_FIELD_GET(DPIO_ANYDL_POWERDOWN_CH1 |
				       DPIO_ALLDL_POWERDOWN_CH1, val);

	drm_WARN(display->drm, actual != expected,
		 "Unexpected DPIO lane power down: all %d, any %d. Expected: all %d, any %d. (0x%x = 0x%08x)\n",
		 !!(actual & DPIO_ALLDL_POWERDOWN),
		 !!(actual & DPIO_ANYDL_POWERDOWN),
		 !!(expected & DPIO_ALLDL_POWERDOWN),
		 !!(expected & DPIO_ANYDL_POWERDOWN),
		 reg, val);
}

bool chv_phy_powergate_ch(struct intel_display *display, enum dpio_phy phy,
			  enum dpio_channel ch, bool override)
{
	struct i915_power_domains *power_domains = &display->power.domains;
	bool was_override;

	mutex_lock(&power_domains->lock);

	was_override = display->power.chv_phy_control & PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	if (override == was_override)
		goto out;

	if (override)
		display->power.chv_phy_control |= PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);
	else
		display->power.chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	intel_de_write(display, DISPLAY_PHY_CONTROL,
		       display->power.chv_phy_control);

	drm_dbg_kms(display->drm,
		    "Power gating DPIO PHY%d CH%d (DPIO_PHY_CONTROL=0x%08x)\n",
		    phy, ch, display->power.chv_phy_control);

	assert_chv_phy_status(display);

out:
	mutex_unlock(&power_domains->lock);

	return was_override;
}

void chv_phy_powergate_lanes(struct intel_encoder *encoder,
			     bool override, unsigned int mask)
{
	struct intel_display *display = to_intel_display(encoder);
	struct i915_power_domains *power_domains = &display->power.domains;
	enum dpio_phy phy = vlv_dig_port_to_phy(enc_to_dig_port(encoder));
	enum dpio_channel ch = vlv_dig_port_to_channel(enc_to_dig_port(encoder));

	mutex_lock(&power_domains->lock);

	display->power.chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD(0xf, phy, ch);
	display->power.chv_phy_control |= PHY_CH_POWER_DOWN_OVRD(mask, phy, ch);

	if (override)
		display->power.chv_phy_control |= PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);
	else
		display->power.chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	intel_de_write(display, DISPLAY_PHY_CONTROL,
		       display->power.chv_phy_control);

	drm_dbg_kms(display->drm,
		    "Power gating DPIO PHY%d CH%d lanes 0x%x (PHY_CONTROL=0x%08x)\n",
		    phy, ch, mask, display->power.chv_phy_control);

	assert_chv_phy_status(display);

	assert_chv_phy_powergate(display, phy, ch, override, mask);

	mutex_unlock(&power_domains->lock);
}

static bool chv_pipe_power_well_enabled(struct intel_display *display,
					struct i915_power_well *power_well)
{
	enum pipe pipe = PIPE_A;
	bool enabled;
	u32 state, ctrl;

	vlv_punit_get(display->drm);

	state = vlv_punit_read(display->drm, PUNIT_REG_DSPSSPM) & DP_SSS_MASK(pipe);
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	drm_WARN_ON(display->drm, state != DP_SSS_PWR_ON(pipe) &&
		    state != DP_SSS_PWR_GATE(pipe));
	enabled = state == DP_SSS_PWR_ON(pipe);

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(display->drm, PUNIT_REG_DSPSSPM) & DP_SSC_MASK(pipe);
	drm_WARN_ON(display->drm, ctrl << 16 != state);

	vlv_punit_put(display->drm);

	return enabled;
}

static void chv_set_pipe_power_well(struct intel_display *display,
				    struct i915_power_well *power_well,
				    bool enable)
{
	enum pipe pipe = PIPE_A;
	u32 state;
	u32 ctrl;

	state = enable ? DP_SSS_PWR_ON(pipe) : DP_SSS_PWR_GATE(pipe);

	vlv_punit_get(display->drm);

#define COND \
	((vlv_punit_read(display->drm, PUNIT_REG_DSPSSPM) & DP_SSS_MASK(pipe)) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(display->drm, PUNIT_REG_DSPSSPM);
	ctrl &= ~DP_SSC_MASK(pipe);
	ctrl |= enable ? DP_SSC_PWR_ON(pipe) : DP_SSC_PWR_GATE(pipe);
	vlv_punit_write(display->drm, PUNIT_REG_DSPSSPM, ctrl);

	if (wait_for(COND, 100))
		drm_err(display->drm,
			"timeout setting power well state %08x (%08x)\n",
			state,
			vlv_punit_read(display->drm, PUNIT_REG_DSPSSPM));

#undef COND

out:
	vlv_punit_put(display->drm);
}

static void chv_pipe_power_well_sync_hw(struct intel_display *display,
					struct i915_power_well *power_well)
{
	intel_de_write(display, DISPLAY_PHY_CONTROL,
		       display->power.chv_phy_control);
}

static void chv_pipe_power_well_enable(struct intel_display *display,
				       struct i915_power_well *power_well)
{
	chv_set_pipe_power_well(display, power_well, true);

	vlv_display_power_well_init(display);
}

static void chv_pipe_power_well_disable(struct intel_display *display,
					struct i915_power_well *power_well)
{
	vlv_display_power_well_deinit(display);

	chv_set_pipe_power_well(display, power_well, false);
}

static void
tgl_tc_cold_request(struct intel_display *display, bool block)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	u8 tries = 0;
	int ret;

	while (1) {
		u32 low_val;
		u32 high_val = 0;

		if (block)
			low_val = TGL_PCODE_EXIT_TCCOLD_DATA_L_BLOCK_REQ;
		else
			low_val = TGL_PCODE_EXIT_TCCOLD_DATA_L_UNBLOCK_REQ;

		/*
		 * Spec states that we should timeout the request after 200us
		 * but the function below will timeout after 500us
		 */
		ret = intel_pcode_read(display->drm, TGL_PCODE_TCCOLD, &low_val, &high_val);
		if (ret == 0) {
			if (block &&
			    (low_val & TGL_PCODE_EXIT_TCCOLD_DATA_L_EXIT_FAILED))
				ret = -EIO;
			else
				break;
		}

		if (++tries == 3)
			break;

		msleep(1);
	}

	if (ret)
		drm_err(&i915->drm, "TC cold %sblock failed\n",
			block ? "" : "un");
	else
		drm_dbg_kms(&i915->drm, "TC cold %sblock succeeded\n",
			    block ? "" : "un");
}

static void
tgl_tc_cold_off_power_well_enable(struct intel_display *display,
				  struct i915_power_well *power_well)
{
	tgl_tc_cold_request(display, true);
}

static void
tgl_tc_cold_off_power_well_disable(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	tgl_tc_cold_request(display, false);
}

static void
tgl_tc_cold_off_power_well_sync_hw(struct intel_display *display,
				   struct i915_power_well *power_well)
{
	if (intel_power_well_refcount(power_well) > 0)
		tgl_tc_cold_off_power_well_enable(display, power_well);
	else
		tgl_tc_cold_off_power_well_disable(display, power_well);
}

static bool
tgl_tc_cold_off_power_well_is_enabled(struct intel_display *display,
				      struct i915_power_well *power_well)
{
	/*
	 * Not the correctly implementation but there is no way to just read it
	 * from PCODE, so returning count to avoid state mismatch errors
	 */
	return intel_power_well_refcount(power_well);
}

static void xelpdp_aux_power_well_enable(struct intel_display *display,
					 struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = i915_power_well_instance(power_well)->xelpdp.aux_ch;
	enum phy phy = icl_aux_pw_to_phy(display, power_well);

	if (intel_phy_is_tc(display, phy))
		icl_tc_port_assert_ref_held(display, power_well,
					    aux_ch_to_digital_port(display, aux_ch));

	intel_de_rmw(display, XELPDP_DP_AUX_CH_CTL(display, aux_ch),
		     XELPDP_DP_AUX_CH_CTL_POWER_REQUEST,
		     XELPDP_DP_AUX_CH_CTL_POWER_REQUEST);

	/*
	 * The power status flag cannot be used to determine whether aux
	 * power wells have finished powering up.  Instead we're
	 * expected to just wait a fixed 600us after raising the request
	 * bit.
	 */
	usleep_range(600, 1200);
}

static void xelpdp_aux_power_well_disable(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = i915_power_well_instance(power_well)->xelpdp.aux_ch;

	intel_de_rmw(display, XELPDP_DP_AUX_CH_CTL(display, aux_ch),
		     XELPDP_DP_AUX_CH_CTL_POWER_REQUEST,
		     0);
	usleep_range(10, 30);
}

static bool xelpdp_aux_power_well_enabled(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = i915_power_well_instance(power_well)->xelpdp.aux_ch;

	return intel_de_read(display, XELPDP_DP_AUX_CH_CTL(display, aux_ch)) &
		XELPDP_DP_AUX_CH_CTL_POWER_STATUS;
}

static void xe2lpd_pica_power_well_enable(struct intel_display *display,
					  struct i915_power_well *power_well)
{
	intel_de_write(display, XE2LPD_PICA_PW_CTL,
		       XE2LPD_PICA_CTL_POWER_REQUEST);

	if (intel_de_wait_for_set(display, XE2LPD_PICA_PW_CTL,
				  XE2LPD_PICA_CTL_POWER_STATUS, 1)) {
		drm_dbg_kms(display->drm, "pica power well enable timeout\n");

		drm_WARN(display->drm, 1, "Power well PICA timeout when enabled");
	}
}

static void xe2lpd_pica_power_well_disable(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	intel_de_write(display, XE2LPD_PICA_PW_CTL, 0);

	if (intel_de_wait_for_clear(display, XE2LPD_PICA_PW_CTL,
				    XE2LPD_PICA_CTL_POWER_STATUS, 1)) {
		drm_dbg_kms(display->drm, "pica power well disable timeout\n");

		drm_WARN(display->drm, 1, "Power well PICA timeout when disabled");
	}
}

static bool xe2lpd_pica_power_well_enabled(struct intel_display *display,
					   struct i915_power_well *power_well)
{
	return intel_de_read(display, XE2LPD_PICA_PW_CTL) &
		XE2LPD_PICA_CTL_POWER_STATUS;
}

const struct i915_power_well_ops i9xx_always_on_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = i9xx_always_on_power_well_noop,
	.disable = i9xx_always_on_power_well_noop,
	.is_enabled = i9xx_always_on_power_well_enabled,
};

const struct i915_power_well_ops chv_pipe_power_well_ops = {
	.sync_hw = chv_pipe_power_well_sync_hw,
	.enable = chv_pipe_power_well_enable,
	.disable = chv_pipe_power_well_disable,
	.is_enabled = chv_pipe_power_well_enabled,
};

const struct i915_power_well_ops chv_dpio_cmn_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = chv_dpio_cmn_power_well_enable,
	.disable = chv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

const struct i915_power_well_ops i830_pipes_power_well_ops = {
	.sync_hw = i830_pipes_power_well_sync_hw,
	.enable = i830_pipes_power_well_enable,
	.disable = i830_pipes_power_well_disable,
	.is_enabled = i830_pipes_power_well_enabled,
};

static const struct i915_power_well_regs hsw_power_well_regs = {
	.bios	= HSW_PWR_WELL_CTL1,
	.driver	= HSW_PWR_WELL_CTL2,
	.kvmr	= HSW_PWR_WELL_CTL3,
	.debug	= HSW_PWR_WELL_CTL4,
};

const struct i915_power_well_ops hsw_power_well_ops = {
	.regs = &hsw_power_well_regs,
	.sync_hw = hsw_power_well_sync_hw,
	.enable = hsw_power_well_enable,
	.disable = hsw_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

const struct i915_power_well_ops gen9_dc_off_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = gen9_dc_off_power_well_enable,
	.disable = gen9_dc_off_power_well_disable,
	.is_enabled = gen9_dc_off_power_well_enabled,
};

const struct i915_power_well_ops bxt_dpio_cmn_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = bxt_dpio_cmn_power_well_enable,
	.disable = bxt_dpio_cmn_power_well_disable,
	.is_enabled = bxt_dpio_cmn_power_well_enabled,
};

const struct i915_power_well_ops vlv_display_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = vlv_display_power_well_enable,
	.disable = vlv_display_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

const struct i915_power_well_ops vlv_dpio_cmn_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = vlv_dpio_cmn_power_well_enable,
	.disable = vlv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

const struct i915_power_well_ops vlv_dpio_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = vlv_power_well_enable,
	.disable = vlv_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_regs icl_aux_power_well_regs = {
	.bios	= ICL_PWR_WELL_CTL_AUX1,
	.driver	= ICL_PWR_WELL_CTL_AUX2,
	.debug	= ICL_PWR_WELL_CTL_AUX4,
};

const struct i915_power_well_ops icl_aux_power_well_ops = {
	.regs = &icl_aux_power_well_regs,
	.sync_hw = hsw_power_well_sync_hw,
	.enable = icl_aux_power_well_enable,
	.disable = icl_aux_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

static const struct i915_power_well_regs icl_ddi_power_well_regs = {
	.bios	= ICL_PWR_WELL_CTL_DDI1,
	.driver	= ICL_PWR_WELL_CTL_DDI2,
	.debug	= ICL_PWR_WELL_CTL_DDI4,
};

const struct i915_power_well_ops icl_ddi_power_well_ops = {
	.regs = &icl_ddi_power_well_regs,
	.sync_hw = hsw_power_well_sync_hw,
	.enable = hsw_power_well_enable,
	.disable = hsw_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

const struct i915_power_well_ops tgl_tc_cold_off_ops = {
	.sync_hw = tgl_tc_cold_off_power_well_sync_hw,
	.enable = tgl_tc_cold_off_power_well_enable,
	.disable = tgl_tc_cold_off_power_well_disable,
	.is_enabled = tgl_tc_cold_off_power_well_is_enabled,
};

const struct i915_power_well_ops xelpdp_aux_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = xelpdp_aux_power_well_enable,
	.disable = xelpdp_aux_power_well_disable,
	.is_enabled = xelpdp_aux_power_well_enabled,
};

const struct i915_power_well_ops xe2lpd_pica_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = xe2lpd_pica_power_well_enable,
	.disable = xe2lpd_pica_power_well_disable,
	.is_enabled = xe2lpd_pica_power_well_enabled,
};
