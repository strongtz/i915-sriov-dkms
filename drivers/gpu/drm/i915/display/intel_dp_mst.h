/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_MST_H__
#define __INTEL_DP_MST_H__

#include <extraversion.h>

#if defined RELEASE_DEBIAN && defined EXTRAVERSION_PVE

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6,5,13)) \
 || (LINUX_VERSION_CODE == KERNEL_VERSION(6,5,13) && EXTRAVERSION_CODE < EXTRAVERSION(4,0))
#define DRM_DP_CALC_PBN_MODE(clock,bpp,dsc) drm_dp_calc_pbn_mode(clock,bpp,dsc)
#else
#define DRM_DP_CALC_PBN_MODE(clock,bpp,dsc) drm_dp_calc_pbn_mode(clock,bpp)
#endif

#elif defined RELEASE_UBUNTU

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6,5,0)) \
 || (LINUX_VERSION_CODE == KERNEL_VERSION(6,5,0) && EXTRAVERSION_CODE < EXTRAVERSION(41,0))
#define DRM_DP_CALC_PBN_MODE(clock,bpp,dsc) drm_dp_calc_pbn_mode(clock,bpp,dsc)
#else
#define DRM_DP_CALC_PBN_MODE(clock,bpp,dsc) drm_dp_calc_pbn_mode(clock,bpp)
#endif

#else

/* This is valid for Debian */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,6,14)
#define DRM_DP_CALC_PBN_MODE(clock,bpp,dsc) drm_dp_calc_pbn_mode(clock,bpp,dsc)
#else
#define DRM_DP_CALC_PBN_MODE(clock,bpp,dsc) drm_dp_calc_pbn_mode(clock,bpp)
#endif

#endif

#include <linux/types.h>

struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_dp;

int intel_dp_mst_encoder_init(struct intel_digital_port *dig_port, int conn_id);
void intel_dp_mst_encoder_cleanup(struct intel_digital_port *dig_port);
int intel_dp_mst_encoder_active_links(struct intel_digital_port *dig_port);
bool intel_dp_mst_is_master_trans(const struct intel_crtc_state *crtc_state);
bool intel_dp_mst_is_slave_trans(const struct intel_crtc_state *crtc_state);
bool intel_dp_mst_source_support(struct intel_dp *intel_dp);
int intel_dp_mst_add_topology_state_for_crtc(struct intel_atomic_state *state,
					     struct intel_crtc *crtc);

#endif /* __INTEL_DP_MST_H__ */
