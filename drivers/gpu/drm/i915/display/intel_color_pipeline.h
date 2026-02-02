/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 19, 0)
#ifndef __INTEL_COLOR_PIPELINE_H__
#define __INTEL_COLOR_PIPELINE_H__

struct drm_plane;
enum pipe;

int intel_color_pipeline_plane_init(struct drm_plane *plane, enum pipe pipe);

#endif /* __INTEL_COLOR_PIPELINE_H__ */
#endif
