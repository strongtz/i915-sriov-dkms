// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_CX0_PHY_H__
#define __INTEL_CX0_PHY_H__

#include <linux/types.h>
#include <linux/bitfield.h>
#include <linux/bits.h>

#include "i915_drv.h"
#include "intel_display_types.h"

struct drm_i915_private;
struct intel_encoder;
struct intel_crtc_state;
enum phy;

#define INTEL_CX0_LANE0		0x0
#define INTEL_CX0_LANE1		0x1
#define INTEL_CX0_BOTH_LANES	0x2

#define MB_WRITE_COMMITTED		1
#define MB_WRITE_UNCOMMITTED		0

bool intel_is_c10phy(struct drm_i915_private *dev_priv, enum phy phy);
void intel_mtl_pll_enable(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state);
void intel_mtl_pll_disable(struct intel_encoder *encoder);
void intel_c10mpllb_readout_hw_state(struct intel_encoder *encoder,
				     struct intel_c10mpllb_state *pll_state);
int intel_cx0mpllb_calc_state(struct intel_crtc_state *crtc_state,
			      struct intel_encoder *encoder);
int intel_c20pll_calc_state(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder);
void intel_c20pll_readout_hw_state(struct intel_encoder *encoder,
				   struct intel_c20pll_state *pll_state);
void intel_c10mpllb_dump_hw_state(struct drm_i915_private *dev_priv,
				  const struct intel_c10mpllb_state *hw_state);
int intel_c10mpllb_calc_port_clock(struct intel_encoder *encoder,
				   const struct intel_c10mpllb_state *pll_state);
void intel_c10mpllb_state_verify(struct intel_atomic_state *state,
				 struct intel_crtc_state *new_crtc_state);
int intel_c20pll_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_c20pll_state *pll_state);
int intel_cx0_phy_check_hdmi_link_rate(struct intel_hdmi *hdmi, int clock);
void intel_cx0_phy_set_signal_levels(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state);

int intel_c20_phy_check_hdmi_link_rate(int clock);
void intel_cx0_phy_ddi_vswing_sequence(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state,
				       u32 level);
int intel_mtl_tbt_calc_port_clock(struct intel_encoder *encoder);
#endif /* __INTEL_CX0_PHY_H__ */
