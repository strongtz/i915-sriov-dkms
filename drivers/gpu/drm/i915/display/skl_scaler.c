// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_utils.h"
#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"

/*
 * The hardware phase 0.0 refers to the center of the pixel.
 * We want to start from the top/left edge which is phase
 * -0.5. That matches how the hardware calculates the scaling
 * factors (from top-left of the first pixel to bottom-right
 * of the last pixel, as opposed to the pixel centers).
 *
 * For 4:2:0 subsampled chroma planes we obviously have to
 * adjust that so that the chroma sample position lands in
 * the right spot.
 *
 * Note that for packed YCbCr 4:2:2 formats there is no way to
 * control chroma siting. The hardware simply replicates the
 * chroma samples for both of the luma samples, and thus we don't
 * actually get the expected MPEG2 chroma siting convention :(
 * The same behaviour is observed on pre-SKL platforms as well.
 *
 * Theory behind the formula (note that we ignore sub-pixel
 * source coordinates):
 * s = source sample position
 * d = destination sample position
 *
 * Downscaling 4:1:
 * -0.5
 * | 0.0
 * | |     1.5 (initial phase)
 * | |     |
 * v v     v
 * | s | s | s | s |
 * |       d       |
 *
 * Upscaling 1:4:
 * -0.5
 * | -0.375 (initial phase)
 * | |     0.0
 * | |     |
 * v v     v
 * |       s       |
 * | d | d | d | d |
 */
static u16 skl_scaler_calc_phase(int sub, int scale, bool chroma_cosited)
{
	int phase = -0x8000;
	u16 trip = 0;

	if (chroma_cosited)
		phase += (sub - 1) * 0x8000 / sub;

	phase += scale / (2 * sub);

	/*
	 * Hardware initial phase limited to [-0.5:1.5].
	 * Since the max hardware scale factor is 3.0, we
	 * should never actually exceed 1.0 here.
	 */
	WARN_ON(phase < -0x8000 || phase > 0x18000);

	if (phase < 0)
		phase = 0x10000 + phase;
	else
		trip = PS_PHASE_TRIP;

	return ((phase >> 2) & PS_PHASE_MASK) | trip;
}

static void skl_scaler_min_src_size(const struct drm_format_info *format,
				    u64 modifier, int *min_w, int *min_h)
{
	if (format && intel_format_info_is_yuv_semiplanar(format, modifier)) {
		*min_w = 16;
		*min_h = 16;
	} else {
		*min_w = 8;
		*min_h = 8;
	}
}

static void skl_scaler_max_src_size(struct intel_crtc *crtc,
				    int *max_w, int *max_h)
{
	struct intel_display *display = to_intel_display(crtc);

	if (DISPLAY_VER(display) >= 14) {
		*max_w = 4096;
		*max_h = 8192;
	} else if (DISPLAY_VER(display) >= 12) {
		*max_w = 5120;
		*max_h = 8192;
	} else if (DISPLAY_VER(display) == 11) {
		*max_w = 5120;
		*max_h = 4096;
	} else {
		*max_w = 4096;
		*max_h = 4096;
	}
}

static void skl_scaler_min_dst_size(int *min_w, int *min_h)
{
	*min_w = 8;
	*min_h = 8;
}

static void skl_scaler_max_dst_size(struct intel_crtc *crtc,
				    int *max_w, int *max_h)
{
	struct intel_display *display = to_intel_display(crtc);

	if (DISPLAY_VER(display) >= 12) {
		*max_w = 8192;
		*max_h = 8192;
	} else if (DISPLAY_VER(display) == 11) {
		*max_w = 5120;
		*max_h = 4096;
	} else {
		*max_w = 4096;
		*max_h = 4096;
	}
}

static int
skl_update_scaler(struct intel_crtc_state *crtc_state, bool force_detach,
		  unsigned int scaler_user, int *scaler_id,
		  int src_w, int src_h, int dst_w, int dst_h,
		  const struct drm_format_info *format,
		  u64 modifier, bool need_scaler)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	int min_src_w, min_src_h, min_dst_w, min_dst_h;
	int max_src_w, max_src_h, max_dst_w, max_dst_h;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	if (src_w != dst_w || src_h != dst_h)
		need_scaler = true;

	/*
	 * Scaling/fitting not supported in IF-ID mode in GEN9+
	 * TODO: Interlace fetch mode doesn't support YUV420 planar formats.
	 * Once NV12 is enabled, handle it here while allocating scaler
	 * for NV12.
	 */
	if (DISPLAY_VER(display) >= 9 && crtc_state->hw.enable &&
	    need_scaler && adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] scaling not supported with IF-ID mode\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	/*
	 * if plane is being disabled or scaler is no more required or force detach
	 *  - free scaler binded to this plane/crtc
	 *  - in order to do this, update crtc->scaler_usage
	 *
	 * Here scaler state in crtc_state is set free so that
	 * scaler can be assigned to other user. Actual register
	 * update to free the scaler is done in plane/panel-fit programming.
	 * For this purpose crtc/plane_state->scaler_id isn't reset here.
	 */
	if (force_detach || !need_scaler) {
		if (*scaler_id >= 0) {
			scaler_state->scaler_users &= ~(1 << scaler_user);
			scaler_state->scalers[*scaler_id].in_use = false;

			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] scaler_user index %u.%u: "
				    "Staged freeing scaler id %d scaler_users = 0x%x\n",
				    crtc->base.base.id, crtc->base.name,
				    crtc->pipe, scaler_user, *scaler_id,
				    scaler_state->scaler_users);
			*scaler_id = -1;
		}
		return 0;
	}

	skl_scaler_min_src_size(format, modifier, &min_src_w, &min_src_h);
	skl_scaler_max_src_size(crtc, &max_src_w, &max_src_h);

	skl_scaler_min_dst_size(&min_dst_w, &min_dst_h);
	skl_scaler_max_dst_size(crtc, &max_dst_w, &max_dst_h);

	/* range checks */
	if (src_w < min_src_w || src_h < min_src_h ||
	    dst_w < min_dst_w || dst_h < min_dst_h ||
	    src_w > max_src_w || src_h > max_src_h ||
	    dst_w > max_dst_w || dst_h > max_dst_h) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] scaler_user index %u.%u: src %ux%u dst %ux%u "
			    "size is out of scaler range\n",
			    crtc->base.base.id, crtc->base.name,
			    crtc->pipe, scaler_user, src_w, src_h,
			    dst_w, dst_h);
		return -EINVAL;
	}

	/*
	 * The pipe scaler does not use all the bits of PIPESRC, at least
	 * on the earlier platforms. So even when we're scaling a plane
	 * the *pipe* source size must not be too large. For simplicity
	 * we assume the limits match the scaler destination size limits.
	 * Might not be 100% accurate on all platforms, but good enough for
	 * now.
	 */
	if (pipe_src_w > max_dst_w || pipe_src_h > max_dst_h) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] scaler_user index %u.%u: pipe src size %ux%u "
			    "is out of scaler range\n",
			    crtc->base.base.id, crtc->base.name,
			    crtc->pipe, scaler_user, pipe_src_w, pipe_src_h);
		return -EINVAL;
	}

	/* mark this plane as a scaler user in crtc_state */
	scaler_state->scaler_users |= (1 << scaler_user);
	drm_dbg_kms(display->drm, "[CRTC:%d:%s] scaler_user index %u.%u: "
		    "staged scaling request for %ux%u->%ux%u scaler_users = 0x%x\n",
		    crtc->base.base.id, crtc->base.name,
		    crtc->pipe, scaler_user, src_w, src_h, dst_w, dst_h,
		    scaler_state->scaler_users);

	return 0;
}

int skl_update_scaler_crtc(struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;
	int width, height;

	if (crtc_state->pch_pfit.enabled) {
		width = drm_rect_width(&crtc_state->pch_pfit.dst);
		height = drm_rect_height(&crtc_state->pch_pfit.dst);
	} else {
		width = pipe_mode->crtc_hdisplay;
		height = pipe_mode->crtc_vdisplay;
	}
	return skl_update_scaler(crtc_state, !crtc_state->hw.active,
				 SKL_CRTC_INDEX,
				 &crtc_state->scaler_state.scaler_id,
				 drm_rect_width(&crtc_state->pipe_src),
				 drm_rect_height(&crtc_state->pipe_src),
				 width, height, NULL, 0,
				 crtc_state->pch_pfit.enabled);
}

/**
 * skl_update_scaler_plane - Stages update to scaler state for a given plane.
 * @crtc_state: crtc's scaler state
 * @plane_state: atomic plane state to update
 *
 * Return
 *     0 - scaler_usage updated successfully
 *    error - requested scaling cannot be supported or other error condition
 */
int skl_update_scaler_plane(struct intel_crtc_state *crtc_state,
			    struct intel_plane_state *plane_state)
{
	struct intel_display *display = to_intel_display(plane_state);
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	bool force_detach = !fb || !plane_state->uapi.visible;
	bool need_scaler = false;

	/* Pre-gen11 and SDR planes always need a scaler for planar formats. */
	if (!icl_is_hdr_plane(display, plane->id) &&
	    fb && intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		need_scaler = true;

	return skl_update_scaler(crtc_state, force_detach,
				 drm_plane_index(&plane->base),
				 &plane_state->scaler_id,
				 drm_rect_width(&plane_state->uapi.src) >> 16,
				 drm_rect_height(&plane_state->uapi.src) >> 16,
				 drm_rect_width(&plane_state->uapi.dst),
				 drm_rect_height(&plane_state->uapi.dst),
				 fb ? fb->format : NULL,
				 fb ? fb->modifier : 0,
				 need_scaler);
}

static int intel_allocate_scaler(struct intel_crtc_scaler_state *scaler_state,
				 struct intel_crtc *crtc)
{
	int i;

	for (i = 0; i < crtc->num_scalers; i++) {
		if (scaler_state->scalers[i].in_use)
			continue;

		scaler_state->scalers[i].in_use = true;

		return i;
	}

	return -1;
}

static void
calculate_max_scale(struct intel_crtc *crtc,
		    bool is_yuv_semiplanar,
		    int scaler_id,
		    int *max_hscale, int *max_vscale)
{
	struct intel_display *display = to_intel_display(crtc);

	/*
	 * FIXME: When two scalers are needed, but only one of
	 * them needs to downscale, we should make sure that
	 * the one that needs downscaling support is assigned
	 * as the first scaler, so we don't reject downscaling
	 * unnecessarily.
	 */

	if (DISPLAY_VER(display) >= 14) {
		/*
		 * On versions 14 and up, only the first
		 * scaler supports a vertical scaling factor
		 * of more than 1.0, while a horizontal
		 * scaling factor of 3.0 is supported.
		 */
		*max_hscale = 0x30000 - 1;

		if (scaler_id == 0)
			*max_vscale = 0x30000 - 1;
		else
			*max_vscale = 0x10000;
	} else if (DISPLAY_VER(display) >= 10 || !is_yuv_semiplanar) {
		*max_hscale = 0x30000 - 1;
		*max_vscale = 0x30000 - 1;
	} else {
		*max_hscale = 0x20000 - 1;
		*max_vscale = 0x20000 - 1;
	}
}

static int intel_atomic_setup_scaler(struct intel_crtc_state *crtc_state,
				     int num_scalers_need, struct intel_crtc *crtc,
				     const char *name, int idx,
				     struct intel_plane_state *plane_state,
				     int *scaler_id)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_scaler_state *scaler_state = &crtc_state->scaler_state;
	u32 mode;
	int hscale = 0;
	int vscale = 0;

	if (*scaler_id < 0)
		*scaler_id = intel_allocate_scaler(scaler_state, crtc);

	if (drm_WARN(display->drm, *scaler_id < 0,
		     "Cannot find scaler for %s:%d\n", name, idx))
		return -EINVAL;

	/* set scaler mode */
	if (plane_state && plane_state->hw.fb &&
	    plane_state->hw.fb->format->is_yuv &&
	    plane_state->hw.fb->format->num_planes > 1) {
		struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

		if (DISPLAY_VER(display) == 9) {
			mode = SKL_PS_SCALER_MODE_NV12;
		} else if (icl_is_hdr_plane(display, plane->id)) {
			/*
			 * On gen11+'s HDR planes we only use the scaler for
			 * scaling. They have a dedicated chroma upsampler, so
			 * we don't need the scaler to upsample the UV plane.
			 */
			mode = PS_SCALER_MODE_NORMAL;
		} else {
			struct intel_plane *linked =
				plane_state->planar_linked_plane;

			mode = PS_SCALER_MODE_PLANAR;

			if (linked)
				mode |= PS_BINDING_Y_PLANE(linked->id);
		}
	} else if (DISPLAY_VER(display) >= 10) {
		mode = PS_SCALER_MODE_NORMAL;
	} else if (num_scalers_need == 1 && crtc->num_scalers > 1) {
		/*
		 * when only 1 scaler is in use on a pipe with 2 scalers
		 * scaler 0 operates in high quality (HQ) mode.
		 * In this case use scaler 0 to take advantage of HQ mode
		 */
		scaler_state->scalers[*scaler_id].in_use = false;
		*scaler_id = 0;
		scaler_state->scalers[0].in_use = true;
		mode = SKL_PS_SCALER_MODE_HQ;
	} else {
		mode = SKL_PS_SCALER_MODE_DYN;
	}

	if (plane_state && plane_state->hw.fb) {
		const struct drm_framebuffer *fb = plane_state->hw.fb;
		const struct drm_rect *src = &plane_state->uapi.src;
		const struct drm_rect *dst = &plane_state->uapi.dst;
		int max_hscale, max_vscale;

		calculate_max_scale(crtc,
				    intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier),
				    *scaler_id, &max_hscale, &max_vscale);

		/*
		 * FIXME: We should change the if-else block above to
		 * support HQ vs dynamic scaler properly.
		 */

		/* Check if required scaling is within limits */
		hscale = drm_rect_calc_hscale(src, dst, 1, max_hscale);
		vscale = drm_rect_calc_vscale(src, dst, 1, max_vscale);

		if (hscale < 0 || vscale < 0) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] scaler %d doesn't support required plane scaling\n",
				    crtc->base.base.id, crtc->base.name, *scaler_id);
			drm_rect_debug_print("src: ", src, true);
			drm_rect_debug_print("dst: ", dst, false);

			return -EINVAL;
		}
	}

	if (crtc_state->pch_pfit.enabled) {
		struct drm_rect src;
		int max_hscale, max_vscale;

		drm_rect_init(&src, 0, 0,
			      drm_rect_width(&crtc_state->pipe_src) << 16,
			      drm_rect_height(&crtc_state->pipe_src) << 16);

		calculate_max_scale(crtc, 0, *scaler_id,
				    &max_hscale, &max_vscale);

		/*
		 * When configured for Pipe YUV 420 encoding for port output,
		 * limit downscaling to less than 1.5 (source/destination) in
		 * the horizontal direction and 1.0 in the vertical direction.
		 */
		if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420) {
			max_hscale = 0x18000 - 1;
			max_vscale = 0x10000;
		}

		hscale = drm_rect_calc_hscale(&src, &crtc_state->pch_pfit.dst,
					      0, max_hscale);
		vscale = drm_rect_calc_vscale(&src, &crtc_state->pch_pfit.dst,
					      0, max_vscale);

		if (hscale < 0 || vscale < 0) {
			drm_dbg_kms(display->drm,
				    "Scaler %d doesn't support required pipe scaling\n",
				    *scaler_id);
			drm_rect_debug_print("src: ", &src, true);
			drm_rect_debug_print("dst: ", &crtc_state->pch_pfit.dst, false);

			return -EINVAL;
		}
	}

	scaler_state->scalers[*scaler_id].hscale = hscale;
	scaler_state->scalers[*scaler_id].vscale = vscale;

	drm_dbg_kms(display->drm, "[CRTC:%d:%s] attached scaler id %u.%u to %s:%d\n",
		    crtc->base.base.id, crtc->base.name,
		    crtc->pipe, *scaler_id, name, idx);
	scaler_state->scalers[*scaler_id].mode = mode;

	return 0;
}

static int setup_crtc_scaler(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;

	return intel_atomic_setup_scaler(crtc_state,
					 hweight32(scaler_state->scaler_users),
					 crtc, "CRTC", crtc->base.base.id,
					 NULL, &scaler_state->scaler_id);
}

static int setup_plane_scaler(struct intel_atomic_state *state,
			      struct intel_crtc *crtc,
			      struct intel_plane *plane)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct intel_plane_state *plane_state;

	/* plane on different crtc cannot be a scaler user of this crtc */
	if (drm_WARN_ON(display->drm, plane->pipe != crtc->pipe))
		return 0;

	plane_state = intel_atomic_get_new_plane_state(state, plane);

	/*
	 * GLK+ scalers don't have a HQ mode so it
	 * isn't necessary to change between HQ and dyn mode
	 * on those platforms.
	 */
	if (!plane_state && DISPLAY_VER(display) >= 10)
		return 0;

	plane_state = intel_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state))
		return PTR_ERR(plane_state);

	return intel_atomic_setup_scaler(crtc_state,
					 hweight32(scaler_state->scaler_users),
					 crtc, "PLANE", plane->base.base.id,
					 plane_state, &plane_state->scaler_id);
}

/**
 * intel_atomic_setup_scalers() - setup scalers for crtc per staged requests
 * @state: atomic state
 * @crtc: crtc
 *
 * This function sets up scalers based on staged scaling requests for
 * a @crtc and its planes. It is called from crtc level check path. If request
 * is a supportable request, it attaches scalers to requested planes and crtc.
 *
 * This function takes into account the current scaler(s) in use by any planes
 * not being part of this atomic state
 *
 *  Returns:
 *         0 - scalers were setup successfully
 *         error code - otherwise
 */
int intel_atomic_setup_scalers(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	int num_scalers_need;
	int i;

	num_scalers_need = hweight32(scaler_state->scaler_users);

	/*
	 * High level flow:
	 * - staged scaler requests are already in scaler_state->scaler_users
	 * - check whether staged scaling requests can be supported
	 * - add planes using scalers that aren't in current transaction
	 * - assign scalers to requested users
	 * - as part of plane commit, scalers will be committed
	 *   (i.e., either attached or detached) to respective planes in hw
	 * - as part of crtc_commit, scaler will be either attached or detached
	 *   to crtc in hw
	 */

	/* fail if required scalers > available scalers */
	if (num_scalers_need > crtc->num_scalers) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] too many scaling requests %d > %d\n",
			    crtc->base.base.id, crtc->base.name,
			    num_scalers_need, crtc->num_scalers);
		return -EINVAL;
	}

	/* walkthrough scaler_users bits and start assigning scalers */
	for (i = 0; i < sizeof(scaler_state->scaler_users) * 8; i++) {
		int ret;

		/* skip if scaler not required */
		if (!(scaler_state->scaler_users & (1 << i)))
			continue;

		if (i == SKL_CRTC_INDEX) {
			ret = setup_crtc_scaler(state, crtc);
			if (ret)
				return ret;
		} else {
			struct intel_plane *plane =
				to_intel_plane(drm_plane_from_index(display->drm, i));

			ret = setup_plane_scaler(state, crtc, plane);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int glk_coef_tap(int i)
{
	return i % 7;
}

static u16 glk_nearest_filter_coef(int t)
{
	return t == 3 ? 0x0800 : 0x3000;
}

/*
 *  Theory behind setting nearest-neighbor integer scaling:
 *
 *  17 phase of 7 taps requires 119 coefficients in 60 dwords per set.
 *  The letter represents the filter tap (D is the center tap) and the number
 *  represents the coefficient set for a phase (0-16).
 *
 *         +------------+--------------------------+--------------------------+
 *         |Index value | Data value coefficient 1 | Data value coefficient 2 |
 *         +------------+--------------------------+--------------------------+
 *         |   00h      |          B0              |          A0              |
 *         +------------+--------------------------+--------------------------+
 *         |   01h      |          D0              |          C0              |
 *         +------------+--------------------------+--------------------------+
 *         |   02h      |          F0              |          E0              |
 *         +------------+--------------------------+--------------------------+
 *         |   03h      |          A1              |          G0              |
 *         +------------+--------------------------+--------------------------+
 *         |   04h      |          C1              |          B1              |
 *         +------------+--------------------------+--------------------------+
 *         |   ...      |          ...             |          ...             |
 *         +------------+--------------------------+--------------------------+
 *         |   38h      |          B16             |          A16             |
 *         +------------+--------------------------+--------------------------+
 *         |   39h      |          D16             |          C16             |
 *         +------------+--------------------------+--------------------------+
 *         |   3Ah      |          F16             |          C16             |
 *         +------------+--------------------------+--------------------------+
 *         |   3Bh      |        Reserved          |          G16             |
 *         +------------+--------------------------+--------------------------+
 *
 *  To enable nearest-neighbor scaling:  program scaler coefficients with
 *  the center tap (Dxx) values set to 1 and all other values set to 0 as per
 *  SCALER_COEFFICIENT_FORMAT
 *
 */

static void glk_program_nearest_filter_coefs(struct intel_display *display,
					     struct intel_dsb *dsb,
					     enum pipe pipe, int id, int set)
{
	int i;

	intel_de_write_dsb(display, dsb,
			   GLK_PS_COEF_INDEX_SET(pipe, id, set),
			   PS_COEF_INDEX_AUTO_INC);

	for (i = 0; i < 17 * 7; i += 2) {
		u32 tmp;
		int t;

		t = glk_coef_tap(i);
		tmp = glk_nearest_filter_coef(t);

		t = glk_coef_tap(i + 1);
		tmp |= glk_nearest_filter_coef(t) << 16;

		intel_de_write_dsb(display, dsb,
				   GLK_PS_COEF_DATA_SET(pipe, id, set), tmp);
	}

	intel_de_write_dsb(display, dsb,
			   GLK_PS_COEF_INDEX_SET(pipe, id, set), 0);
}

static u32 skl_scaler_get_filter_select(enum drm_scaling_filter filter)
{
	if (filter == DRM_SCALING_FILTER_NEAREST_NEIGHBOR)
		return (PS_FILTER_PROGRAMMED |
			PS_Y_VERT_FILTER_SELECT(0) |
			PS_Y_HORZ_FILTER_SELECT(0) |
			PS_UV_VERT_FILTER_SELECT(0) |
			PS_UV_HORZ_FILTER_SELECT(0));

	return PS_FILTER_MEDIUM;
}

static void skl_scaler_setup_filter(struct intel_display *display,
				    struct intel_dsb *dsb, enum pipe pipe,
				    int id, int set, enum drm_scaling_filter filter)
{
	switch (filter) {
	case DRM_SCALING_FILTER_DEFAULT:
		break;
	case DRM_SCALING_FILTER_NEAREST_NEIGHBOR:
		glk_program_nearest_filter_coefs(display, dsb, pipe, id, set);
		break;
	default:
		MISSING_CASE(filter);
	}
}

void skl_pfit_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	const struct drm_rect *dst = &crtc_state->pch_pfit.dst;
	u16 uv_rgb_hphase, uv_rgb_vphase;
	enum pipe pipe = crtc->pipe;
	int width = drm_rect_width(dst);
	int height = drm_rect_height(dst);
	int x = dst->x1;
	int y = dst->y1;
	int hscale, vscale;
	struct drm_rect src;
	int id;
	u32 ps_ctrl;

	if (!crtc_state->pch_pfit.enabled)
		return;

	if (drm_WARN_ON(display->drm,
			crtc_state->scaler_state.scaler_id < 0))
		return;

	drm_rect_init(&src, 0, 0,
		      drm_rect_width(&crtc_state->pipe_src) << 16,
		      drm_rect_height(&crtc_state->pipe_src) << 16);

	hscale = drm_rect_calc_hscale(&src, dst, 0, INT_MAX);
	vscale = drm_rect_calc_vscale(&src, dst, 0, INT_MAX);

	uv_rgb_hphase = skl_scaler_calc_phase(1, hscale, false);
	uv_rgb_vphase = skl_scaler_calc_phase(1, vscale, false);

	id = scaler_state->scaler_id;

	ps_ctrl = PS_SCALER_EN | PS_BINDING_PIPE | scaler_state->scalers[id].mode |
		skl_scaler_get_filter_select(crtc_state->hw.scaling_filter);

	trace_intel_pipe_scaler_update_arm(crtc, id, x, y, width, height);

	skl_scaler_setup_filter(display, NULL, pipe, id, 0,
				crtc_state->hw.scaling_filter);

	intel_de_write_fw(display, SKL_PS_CTRL(pipe, id), ps_ctrl);

	intel_de_write_fw(display, SKL_PS_VPHASE(pipe, id),
			  PS_Y_PHASE(0) | PS_UV_RGB_PHASE(uv_rgb_vphase));
	intel_de_write_fw(display, SKL_PS_HPHASE(pipe, id),
			  PS_Y_PHASE(0) | PS_UV_RGB_PHASE(uv_rgb_hphase));
	intel_de_write_fw(display, SKL_PS_WIN_POS(pipe, id),
			  PS_WIN_XPOS(x) | PS_WIN_YPOS(y));
	intel_de_write_fw(display, SKL_PS_WIN_SZ(pipe, id),
			  PS_WIN_XSIZE(width) | PS_WIN_YSIZE(height));
}

void
skl_program_plane_scaler(struct intel_dsb *dsb,
			 struct intel_plane *plane,
			 const struct intel_crtc_state *crtc_state,
			 const struct intel_plane_state *plane_state)
{
	struct intel_display *display = to_intel_display(plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	int scaler_id = plane_state->scaler_id;
	const struct intel_scaler *scaler =
		&crtc_state->scaler_state.scalers[scaler_id];
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	u32 crtc_w = drm_rect_width(&plane_state->uapi.dst);
	u32 crtc_h = drm_rect_height(&plane_state->uapi.dst);
	u16 y_hphase, uv_rgb_hphase;
	u16 y_vphase, uv_rgb_vphase;
	int hscale, vscale;
	u32 ps_ctrl;

	hscale = drm_rect_calc_hscale(&plane_state->uapi.src,
				      &plane_state->uapi.dst,
				      0, INT_MAX);
	vscale = drm_rect_calc_vscale(&plane_state->uapi.src,
				      &plane_state->uapi.dst,
				      0, INT_MAX);

	/* TODO: handle sub-pixel coordinates */
	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier) &&
	    !icl_is_hdr_plane(display, plane->id)) {
		y_hphase = skl_scaler_calc_phase(1, hscale, false);
		y_vphase = skl_scaler_calc_phase(1, vscale, false);

		/* MPEG2 chroma siting convention */
		uv_rgb_hphase = skl_scaler_calc_phase(2, hscale, true);
		uv_rgb_vphase = skl_scaler_calc_phase(2, vscale, false);
	} else {
		/* not used */
		y_hphase = 0;
		y_vphase = 0;

		uv_rgb_hphase = skl_scaler_calc_phase(1, hscale, false);
		uv_rgb_vphase = skl_scaler_calc_phase(1, vscale, false);
	}

	ps_ctrl = PS_SCALER_EN | PS_BINDING_PLANE(plane->id) | scaler->mode |
		skl_scaler_get_filter_select(plane_state->hw.scaling_filter);

	trace_intel_plane_scaler_update_arm(plane, scaler_id,
					    crtc_x, crtc_y, crtc_w, crtc_h);

	skl_scaler_setup_filter(display, dsb, pipe, scaler_id, 0,
				plane_state->hw.scaling_filter);

	intel_de_write_dsb(display, dsb, SKL_PS_CTRL(pipe, scaler_id),
			   ps_ctrl);
	intel_de_write_dsb(display, dsb, SKL_PS_VPHASE(pipe, scaler_id),
			   PS_Y_PHASE(y_vphase) | PS_UV_RGB_PHASE(uv_rgb_vphase));
	intel_de_write_dsb(display, dsb, SKL_PS_HPHASE(pipe, scaler_id),
			   PS_Y_PHASE(y_hphase) | PS_UV_RGB_PHASE(uv_rgb_hphase));
	intel_de_write_dsb(display, dsb, SKL_PS_WIN_POS(pipe, scaler_id),
			   PS_WIN_XPOS(crtc_x) | PS_WIN_YPOS(crtc_y));
	intel_de_write_dsb(display, dsb, SKL_PS_WIN_SZ(pipe, scaler_id),
			   PS_WIN_XSIZE(crtc_w) | PS_WIN_YSIZE(crtc_h));
}

static void skl_detach_scaler(struct intel_dsb *dsb,
			      struct intel_crtc *crtc, int id)
{
	struct intel_display *display = to_intel_display(crtc);

	trace_intel_scaler_disable_arm(crtc, id);

	intel_de_write_dsb(display, dsb, SKL_PS_CTRL(crtc->pipe, id), 0);
	intel_de_write_dsb(display, dsb, SKL_PS_WIN_POS(crtc->pipe, id), 0);
	intel_de_write_dsb(display, dsb, SKL_PS_WIN_SZ(crtc->pipe, id), 0);
}

/*
 * This function detaches (aka. unbinds) unused scalers in hardware
 */
void skl_detach_scalers(struct intel_dsb *dsb,
			const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	int i;

	/* loop through and disable scalers that aren't in use */
	for (i = 0; i < crtc->num_scalers; i++) {
		if (!scaler_state->scalers[i].in_use)
			skl_detach_scaler(dsb, crtc, i);
	}
}

void skl_scaler_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	int i;

	for (i = 0; i < crtc->num_scalers; i++)
		skl_detach_scaler(NULL, crtc, i);
}

void skl_scaler_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_crtc_scaler_state *scaler_state = &crtc_state->scaler_state;
	int id = -1;
	int i;

	/* find scaler attached to this pipe */
	for (i = 0; i < crtc->num_scalers; i++) {
		u32 ctl, pos, size;

		ctl = intel_de_read(display, SKL_PS_CTRL(crtc->pipe, i));
		if ((ctl & (PS_SCALER_EN | PS_BINDING_MASK)) != (PS_SCALER_EN | PS_BINDING_PIPE))
			continue;

		id = i;
		crtc_state->pch_pfit.enabled = true;

		pos = intel_de_read(display, SKL_PS_WIN_POS(crtc->pipe, i));
		size = intel_de_read(display, SKL_PS_WIN_SZ(crtc->pipe, i));

		drm_rect_init(&crtc_state->pch_pfit.dst,
			      REG_FIELD_GET(PS_WIN_XPOS_MASK, pos),
			      REG_FIELD_GET(PS_WIN_YPOS_MASK, pos),
			      REG_FIELD_GET(PS_WIN_XSIZE_MASK, size),
			      REG_FIELD_GET(PS_WIN_YSIZE_MASK, size));

		scaler_state->scalers[i].in_use = true;
		break;
	}

	scaler_state->scaler_id = id;
	if (id >= 0)
		scaler_state->scaler_users |= (1 << SKL_CRTC_INDEX);
	else
		scaler_state->scaler_users &= ~(1 << SKL_CRTC_INDEX);
}
