// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/debugfs.h>

#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i9xx_wm.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_wm.h"
#include "skl_watermark.h"

/**
 * intel_update_watermarks - update FIFO watermark values based on current modes
 * @display: display device
 *
 * Calculate watermark values for the various WM regs based on current mode
 * and plane configuration.
 *
 * There are several cases to deal with here:
 *   - normal (i.e. non-self-refresh)
 *   - self-refresh (SR) mode
 *   - lines are large relative to FIFO size (buffer can hold up to 2)
 *   - lines are small relative to FIFO size (buffer can hold more than 2
 *     lines), so need to account for TLB latency
 *
 *   The normal calculation is:
 *     watermark = dotclock * bytes per pixel * latency
 *   where latency is platform & configuration dependent (we assume pessimal
 *   values here).
 *
 *   The SR calculation is:
 *     watermark = (trunc(latency/line time)+1) * surface width *
 *       bytes per pixel
 *   where
 *     line time = htotal / dotclock
 *     surface width = hdisplay for normal plane and 64 for cursor
 *   and latency is assumed to be high, as above.
 *
 * The final value programmed to the register should always be rounded up,
 * and include an extra 2 entries to account for clock crossings.
 *
 * We don't use the sprite, so we can ignore that.  And on Crestline we have
 * to set the non-SR watermarks to 8.
 */
void intel_update_watermarks(struct intel_display *display)
{
	if (display->funcs.wm->update_wm)
		display->funcs.wm->update_wm(display);
}

int intel_wm_compute(struct intel_atomic_state *state,
		     struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);

	if (!display->funcs.wm->compute_watermarks)
		return 0;

	return display->funcs.wm->compute_watermarks(state, crtc);
}

bool intel_initial_watermarks(struct intel_atomic_state *state,
			      struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);

	if (display->funcs.wm->initial_watermarks) {
		display->funcs.wm->initial_watermarks(state, crtc);
		return true;
	}

	return false;
}

void intel_atomic_update_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);

	if (display->funcs.wm->atomic_update_watermarks)
		display->funcs.wm->atomic_update_watermarks(state, crtc);
}

void intel_optimize_watermarks(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);

	if (display->funcs.wm->optimize_watermarks)
		display->funcs.wm->optimize_watermarks(state, crtc);
}

int intel_compute_global_watermarks(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);

	if (display->funcs.wm->compute_global_watermarks)
		return display->funcs.wm->compute_global_watermarks(state);

	return 0;
}

void intel_wm_get_hw_state(struct intel_display *display)
{
	if (display->funcs.wm->get_hw_state)
		return display->funcs.wm->get_hw_state(display);
}

void intel_wm_sanitize(struct intel_display *display)
{
	if (display->funcs.wm->sanitize)
		return display->funcs.wm->sanitize(display);
}

bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

	/* FIXME check the 'enable' instead */
	if (!crtc_state->hw.active)
		return false;

	/*
	 * Treat cursor with fb as always visible since cursor updates
	 * can happen faster than the vrefresh rate, and the current
	 * watermark code doesn't handle that correctly. Cursor updates
	 * which set/clear the fb or change the cursor size are going
	 * to get throttled by intel_legacy_cursor_update() to work
	 * around this problem with the watermark code.
	 */
	if (plane->id == PLANE_CURSOR)
		return plane_state->hw.fb != NULL;
	else
		return plane_state->uapi.visible;
}

void intel_print_wm_latency(struct intel_display *display,
			    const char *name, const u16 wm[])
{
	int level;

	for (level = 0; level < display->wm.num_levels; level++) {
		unsigned int latency = wm[level];

		if (latency == 0) {
			drm_dbg_kms(display->drm,
				    "%s WM%d latency not provided\n",
				    name, level);
			continue;
		}

		/*
		 * - latencies are in us on gen9.
		 * - before then, WM1+ latency values are in 0.5us units
		 */
		if (DISPLAY_VER(display) >= 9)
			latency *= 10;
		else if (level > 0)
			latency *= 5;

		drm_dbg_kms(display->drm,
			    "%s WM%d latency %u (%u.%u usec)\n", name, level,
			    wm[level], latency / 10, latency % 10);
	}
}

static const struct intel_wm_funcs nop_wm_func = {
};

void intel_wm_init(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	if (IS_SRIOV_VF(i915)) {
		/* XXX */
		i915->display->funcs.wm = &nop_wm_func;
		return;
	}
	if (DISPLAY_VER(display) >= 9)
		skl_wm_init(display);
	else
		i9xx_wm_init(display);
}

static void wm_latency_show(struct seq_file *m, const u16 wm[8])
{
	struct intel_display *display = m->private;
	int level;

	drm_modeset_lock_all(display->drm);

	for (level = 0; level < display->wm.num_levels; level++) {
		unsigned int latency = wm[level];

		/*
		 * - WM1+ latency values in 0.5us units
		 * - latencies are in us on gen9/vlv/chv
		 */
		if (DISPLAY_VER(display) >= 9 ||
		    display->platform.valleyview ||
		    display->platform.cherryview ||
		    display->platform.g4x)
			latency *= 10;
		else if (level > 0)
			latency *= 5;

		seq_printf(m, "WM%d %u (%u.%u usec)\n",
			   level, wm[level], latency / 10, latency % 10);
	}

	drm_modeset_unlock_all(display->drm);
}

static int pri_wm_latency_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	const u16 *latencies;

	if (DISPLAY_VER(display) >= 9)
		latencies = display->wm.skl_latency;
	else
		latencies = display->wm.pri_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int spr_wm_latency_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	const u16 *latencies;

	if (DISPLAY_VER(display) >= 9)
		latencies = display->wm.skl_latency;
	else
		latencies = display->wm.spr_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int cur_wm_latency_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	const u16 *latencies;

	if (DISPLAY_VER(display) >= 9)
		latencies = display->wm.skl_latency;
	else
		latencies = display->wm.cur_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int pri_wm_latency_open(struct inode *inode, struct file *file)
{
	struct intel_display *display = inode->i_private;

	if (DISPLAY_VER(display) < 5 && !display->platform.g4x)
		return -ENODEV;

	return single_open(file, pri_wm_latency_show, display);
}

static int spr_wm_latency_open(struct inode *inode, struct file *file)
{
	struct intel_display *display = inode->i_private;

	if (HAS_GMCH(display))
		return -ENODEV;

	return single_open(file, spr_wm_latency_show, display);
}

static int cur_wm_latency_open(struct inode *inode, struct file *file)
{
	struct intel_display *display = inode->i_private;

	if (HAS_GMCH(display))
		return -ENODEV;

	return single_open(file, cur_wm_latency_show, display);
}

static ssize_t wm_latency_write(struct file *file, const char __user *ubuf,
				size_t len, loff_t *offp, u16 wm[8])
{
	struct seq_file *m = file->private_data;
	struct intel_display *display = m->private;
	u16 new[8] = {};
	int level;
	int ret;
	char tmp[32];

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	ret = sscanf(tmp, "%hu %hu %hu %hu %hu %hu %hu %hu",
		     &new[0], &new[1], &new[2], &new[3],
		     &new[4], &new[5], &new[6], &new[7]);
	if (ret != display->wm.num_levels)
		return -EINVAL;

	drm_modeset_lock_all(display->drm);

	for (level = 0; level < display->wm.num_levels; level++)
		wm[level] = new[level];

	drm_modeset_unlock_all(display->drm);

	return len;
}

static ssize_t pri_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_display *display = m->private;
	u16 *latencies;

	if (DISPLAY_VER(display) >= 9)
		latencies = display->wm.skl_latency;
	else
		latencies = display->wm.pri_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static ssize_t spr_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_display *display = m->private;
	u16 *latencies;

	if (DISPLAY_VER(display) >= 9)
		latencies = display->wm.skl_latency;
	else
		latencies = display->wm.spr_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static ssize_t cur_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_display *display = m->private;
	u16 *latencies;

	if (DISPLAY_VER(display) >= 9)
		latencies = display->wm.skl_latency;
	else
		latencies = display->wm.cur_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static const struct file_operations i915_pri_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = pri_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = pri_wm_latency_write
};

static const struct file_operations i915_spr_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = spr_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = spr_wm_latency_write
};

static const struct file_operations i915_cur_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = cur_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = cur_wm_latency_write
};

void intel_wm_debugfs_register(struct intel_display *display)
{
	struct drm_minor *minor = display->drm->primary;

	debugfs_create_file("i915_pri_wm_latency", 0644, minor->debugfs_root,
			    display, &i915_pri_wm_latency_fops);

	debugfs_create_file("i915_spr_wm_latency", 0644, minor->debugfs_root,
			    display, &i915_spr_wm_latency_fops);

	debugfs_create_file("i915_cur_wm_latency", 0644, minor->debugfs_root,
			    display, &i915_cur_wm_latency_fops);

	skl_watermark_debugfs_register(display);
}
