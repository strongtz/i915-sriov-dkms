#include_next <drm/drm_colorop.h>
#ifndef __BACKPORT_DRM_COLOROP_H__
#define __BACKPORT_DRM_COLOROP_H__

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
/*
 * Linux 7.1: drm_plane_colorop_*_init() gained an extra parameter
 * 'const struct drm_colorop_funcs *funcs' (right after 'plane'). The i915 code
 * vendored from 7.0 still calls the old signature without funcs.
 *
 * struct drm_colorop_funcs only holds an optional destroy callback; the
 * vendored intel_colorop.c defines no funcs, so NULL is the semantically
 * correct value ("no driver-specific control funcs").
 *
 * These variadic macros inject NULL at position 4 so the vendored .c code stays
 * UNCHANGED (maximum upstream proximity). The identically named macro does not
 * expand recursively (C "blue paint" rule) -> real function call. The real
 * declarations come via #include_next above, before these macros are defined,
 * so there is no clash with the prototypes.
 *
 * Active only from 7.1; for 6.19..7.0 the old signature already matches.
 */
#define drm_plane_colorop_curve_1d_lut_init(dev, colorop, plane, ...) \
	drm_plane_colorop_curve_1d_lut_init(dev, colorop, plane, NULL, __VA_ARGS__)

#define drm_plane_colorop_ctm_3x4_init(dev, colorop, plane, ...) \
	drm_plane_colorop_ctm_3x4_init(dev, colorop, plane, NULL, __VA_ARGS__)

#define drm_plane_colorop_3dlut_init(dev, colorop, plane, ...) \
	drm_plane_colorop_3dlut_init(dev, colorop, plane, NULL, __VA_ARGS__)
#endif

#endif /* __BACKPORT_DRM_COLOROP_H__ */
