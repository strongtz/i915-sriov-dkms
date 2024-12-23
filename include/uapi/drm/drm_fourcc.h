#include <linux/version.h>
#include_next <uapi/drm/drm_fourcc.h>

#ifndef _BACKPORT_UAPI_DRM_DRM_FOURCC_H
#define _BACKPORT_UAPI_DRM_DRM_FOURCC_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
/*
 * Intel Color Control Surfaces (CCS) for graphics ver. 20 unified compression
 * on integrated graphics
 *
 * The main surface is Tile 4 and at plane index 0. For semi-planar formats
 * like NV12, the Y and UV planes are Tile 4 and are located at plane indices
 * 0 and 1, respectively. The CCS for all planes are stored outside of the
 * GEM object in a reserved memory area dedicated for the storage of the
 * CCS data for all compressible GEM objects.
 */
#define I915_FORMAT_MOD_4_TILED_LNL_CCS fourcc_mod_code(INTEL, 16)
/*
 * Intel Color Control Surfaces (CCS) for graphics ver. 20 unified compression
 * on discrete graphics
 *
 * The main surface is Tile 4 and at plane index 0. For semi-planar formats
 * like NV12, the Y and UV planes are Tile 4 and are located at plane indices
 * 0 and 1, respectively. The CCS for all planes are stored outside of the
 * GEM object in a reserved memory area dedicated for the storage of the
 * CCS data for all compressible GEM objects. The GEM object must be stored in
 * contiguous memory with a size aligned to 64KB
 */
#define I915_FORMAT_MOD_4_TILED_BMG_CCS fourcc_mod_code(INTEL, 17)
#endif

#endif