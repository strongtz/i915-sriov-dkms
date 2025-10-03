/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_VRR_H__
#define __INTEL_VRR_H__

#include <linux/types.h>

struct drm_connector_state;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc_state;
struct intel_dsb;
struct intel_display;

bool intel_vrr_is_capable(struct intel_connector *connector);
bool intel_vrr_is_in_range(struct intel_connector *connector, int vrefresh);
bool intel_vrr_possible(const struct intel_crtc_state *crtc_state);
void intel_vrr_check_modeset(struct intel_atomic_state *state);
void intel_vrr_compute_config(struct intel_crtc_state *crtc_state,
			      struct drm_connector_state *conn_state);
void intel_vrr_compute_config_late(struct intel_crtc_state *crtc_state);
void intel_vrr_set_transcoder_timings(const struct intel_crtc_state *crtc_state);
void intel_vrr_enable(const struct intel_crtc_state *crtc_state);
void intel_vrr_send_push(struct intel_dsb *dsb,
			 const struct intel_crtc_state *crtc_state);
void intel_vrr_check_push_sent(struct intel_dsb *dsb,
			       const struct intel_crtc_state *crtc_state);
bool intel_vrr_is_push_sent(const struct intel_crtc_state *crtc_state);
void intel_vrr_disable(const struct intel_crtc_state *old_crtc_state);
void intel_vrr_get_config(struct intel_crtc_state *crtc_state);
int intel_vrr_vmax_vtotal(const struct intel_crtc_state *crtc_state);
int intel_vrr_vmin_vtotal(const struct intel_crtc_state *crtc_state);
int intel_vrr_vmax_vblank_start(const struct intel_crtc_state *crtc_state);
int intel_vrr_vmin_vblank_start(const struct intel_crtc_state *crtc_state);
int intel_vrr_vblank_delay(const struct intel_crtc_state *crtc_state);
bool intel_vrr_is_fixed_rr(const struct intel_crtc_state *crtc_state);
void intel_vrr_transcoder_enable(const struct intel_crtc_state *crtc_state);
void intel_vrr_transcoder_disable(const struct intel_crtc_state *crtc_state);
void intel_vrr_set_fixed_rr_timings(const struct intel_crtc_state *crtc_state);
bool intel_vrr_always_use_vrr_tg(struct intel_display *display);

#endif /* __INTEL_VRR_H__ */
