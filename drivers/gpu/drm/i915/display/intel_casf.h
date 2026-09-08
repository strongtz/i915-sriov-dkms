/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 19, 0)
#ifndef __INTEL_CASF_H__
#define __INTEL_CASF_H__

#include <linux/types.h>

struct intel_crtc_state;

int intel_casf_compute_config(struct intel_crtc_state *crtc_state);
void intel_casf_sharpness_get_config(struct intel_crtc_state *crtc_state);
void intel_casf_setup(const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_CASF_H__ */
#endif
