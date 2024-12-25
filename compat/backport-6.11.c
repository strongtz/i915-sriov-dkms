/*
 * Copyright (c) 2024
 *
 * Backport functionality introduced in Linux 6.11.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/string_choices.h>
#include <uapi/linux/sched/types.h>

#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_work.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fixed.h>
#include <drm/drm_print.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
/**
 * drm_vblank_work_flush_all - flush all currently pending vblank work on crtc.
 * @crtc: crtc for which vblank work to flush
 *
 * Wait until all currently queued vblank work on @crtc
 * has finished executing once.
 */
void drm_vblank_work_flush_all(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_vblank_crtc *vblank = &dev->vblank[drm_crtc_index(crtc)];
	spin_lock_irq(&dev->event_lock);
	wait_event_lock_irq(vblank->work_wait_queue,
			    list_empty(&vblank->pending_work),
			    dev->event_lock);
	spin_unlock_irq(&dev->event_lock);
	kthread_flush_worker(vblank->worker);
}

/**
 * drm_plane_has_format - Check whether the plane supports this format and modifier combination
 * @plane: drm plane
 * @format: pixel format (DRM_FORMAT_*)
 * @modifier: data layout modifier
 *
 * Returns:
 * Whether the plane supports the specified format and modifier combination.
 */
bool drm_plane_has_format(struct drm_plane *plane,
			  u32 format, u64 modifier)
{
	unsigned int i;

	for (i = 0; i < plane->format_count; i++) {
		if (format == plane->format_types[i])
			break;
	}
	if (i == plane->format_count)
		return false;

	if (plane->funcs->format_mod_supported) {
		if (!plane->funcs->format_mod_supported(plane, format, modifier))
			return false;
	} else {
		if (!plane->modifier_count)
			return true;

		for (i = 0; i < plane->modifier_count; i++) {
			if (modifier == plane->modifiers[i])
				break;
		}
		if (i == plane->modifier_count)
			return false;
	}

	return true;
}

static void drm_dsc_dump_config_main_params(struct drm_printer *p, int indent,
					    const struct drm_dsc_config *cfg)
{
	drm_printf_indent(p, indent,
			  "dsc-cfg: version: %d.%d, picture: w=%d, h=%d, slice: count=%d, w=%d, h=%d, size=%d\n",
			  cfg->dsc_version_major, cfg->dsc_version_minor,
			  cfg->pic_width, cfg->pic_height,
			  cfg->slice_count, cfg->slice_width, cfg->slice_height, cfg->slice_chunk_size);
	drm_printf_indent(p, indent,
			  "dsc-cfg: mode: block-pred=%s, vbr=%s, rgb=%s, simple-422=%s, native-422=%s, native-420=%s\n",
			  str_yes_no(cfg->block_pred_enable), str_yes_no(cfg->vbr_enable),
			  str_yes_no(cfg->convert_rgb),
			  str_yes_no(cfg->simple_422), str_yes_no(cfg->native_422), str_yes_no(cfg->native_420));
	drm_printf_indent(p, indent,
			  "dsc-cfg: color-depth: uncompressed-bpc=%d, compressed-bpp=" FXP_Q4_FMT " line-buf-bpp=%d\n",
			  cfg->bits_per_component, FXP_Q4_ARGS(cfg->bits_per_pixel), cfg->line_buf_depth);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-model: size=%d, bits=%d, mux-word-size: %d, initial-delays: xmit=%d, dec=%d\n",
			  cfg->rc_model_size, cfg->rc_bits, cfg->mux_word_size,
			  cfg->initial_xmit_delay, cfg->initial_dec_delay);
	drm_printf_indent(p, indent,
			  "dsc-cfg: offsets: initial=%d, final=%d, slice-bpg=%d\n",
			  cfg->initial_offset, cfg->final_offset, cfg->slice_bpg_offset);
	drm_printf_indent(p, indent,
			  "dsc-cfg: line-bpg-offsets: first=%d, non-first=%d, second=%d, non-second=%d, second-adj=%d\n",
			  cfg->first_line_bpg_offset, cfg->nfl_bpg_offset,
			  cfg->second_line_bpg_offset, cfg->nsl_bpg_offset, cfg->second_line_offset_adj);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-tgt-offsets: low=%d, high=%d, rc-edge-factor: %d, rc-quant-incr-limits: [0]=%d, [1]=%d\n",
			  cfg->rc_tgt_offset_low, cfg->rc_tgt_offset_high,
			  cfg->rc_edge_factor, cfg->rc_quant_incr_limit0, cfg->rc_quant_incr_limit1);
	drm_printf_indent(p, indent,
			  "dsc-cfg: initial-scale: %d, scale-intervals: increment=%d, decrement=%d\n",
			  cfg->initial_scale_value, cfg->scale_increment_interval, cfg->scale_decrement_interval);
	drm_printf_indent(p, indent,
			  "dsc-cfg: flatness: min-qp=%d, max-qp=%d\n",
			  cfg->flatness_min_qp, cfg->flatness_max_qp);
}

static void drm_dsc_dump_config_rc_params(struct drm_printer *p, int indent,
					  const struct drm_dsc_config *cfg)
{
	const u16 *bt = cfg->rc_buf_thresh;
	const struct drm_dsc_rc_range_parameters *rp = cfg->rc_range_params;

	BUILD_BUG_ON(ARRAY_SIZE(cfg->rc_buf_thresh) != 14);
	BUILD_BUG_ON(ARRAY_SIZE(cfg->rc_range_params) != 15);

	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-level:         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14\n");
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-buf-thresh:  %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  bt[0], bt[1], bt[2],  bt[3],  bt[4],  bt[5], bt[6], bt[7],
			  bt[8], bt[9], bt[10], bt[11], bt[12], bt[13]);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-min-qp:      %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  rp[0].range_min_qp,  rp[1].range_min_qp,  rp[2].range_min_qp,  rp[3].range_min_qp,
			  rp[4].range_min_qp,  rp[5].range_min_qp,  rp[6].range_min_qp,  rp[7].range_min_qp,
			  rp[8].range_min_qp,  rp[9].range_min_qp,  rp[10].range_min_qp, rp[11].range_min_qp,
			  rp[12].range_min_qp, rp[13].range_min_qp, rp[14].range_min_qp);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-max-qp:      %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  rp[0].range_max_qp,  rp[1].range_max_qp,  rp[2].range_max_qp,  rp[3].range_max_qp,
			  rp[4].range_max_qp,  rp[5].range_max_qp,  rp[6].range_max_qp,  rp[7].range_max_qp,
			  rp[8].range_max_qp,  rp[9].range_max_qp,  rp[10].range_max_qp, rp[11].range_max_qp,
			  rp[12].range_max_qp, rp[13].range_max_qp, rp[14].range_max_qp);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-bpg-offset:  %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  rp[0].range_bpg_offset,  rp[1].range_bpg_offset,  rp[2].range_bpg_offset,  rp[3].range_bpg_offset,
			  rp[4].range_bpg_offset,  rp[5].range_bpg_offset,  rp[6].range_bpg_offset,  rp[7].range_bpg_offset,
			  rp[8].range_bpg_offset,  rp[9].range_bpg_offset,  rp[10].range_bpg_offset, rp[11].range_bpg_offset,
			  rp[12].range_bpg_offset, rp[13].range_bpg_offset, rp[14].range_bpg_offset);
}

/**
 * drm_dsc_dump_config - Dump the provided DSC configuration
 * @p: The printer used for output
 * @indent: Tab indentation level (max 5)
 * @cfg: DSC configuration to print
 *
 * Print the provided DSC configuration in @cfg.
 */
void drm_dsc_dump_config(struct drm_printer *p, int indent,
			 const struct drm_dsc_config *cfg)
{
	drm_dsc_dump_config_main_params(p, indent, cfg);
	drm_dsc_dump_config_rc_params(p, indent, cfg);
}

#endif