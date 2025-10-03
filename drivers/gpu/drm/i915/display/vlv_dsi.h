/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __VLV_DSI_H__
#define __VLV_DSI_H__

enum port;
struct intel_crtc_state;
struct intel_display;
struct intel_dsi;

#ifdef I915
void vlv_dsi_wait_for_fifo_empty(struct intel_dsi *intel_dsi, enum port port);
int vlv_dsi_min_cdclk(const struct intel_crtc_state *crtc_state);
void vlv_dsi_init(struct intel_display *display);
#else
static inline void vlv_dsi_wait_for_fifo_empty(struct intel_dsi *intel_dsi, enum port port)
{
}
static inline int vlv_dsi_min_cdclk(const struct intel_crtc_state *crtc_state)
{
	return 0;
}
static inline void vlv_dsi_init(struct intel_display *display)
{
}
#endif

#endif /* __VLV_DSI_H__ */
