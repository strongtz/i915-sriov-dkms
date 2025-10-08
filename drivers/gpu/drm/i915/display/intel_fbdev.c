/*
 * Copyright © 2007 David Airlie
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     David Airlie
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/vga_switcheroo.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
#include <drm/clients/drm_client_setup.h>
#endif
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "i915_vma.h"
#include "intel_bo.h"
#include "intel_display_core.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_fbdev.h"
#include "intel_fbdev_fb.h"
#include "intel_frontbuffer.h"

#include "i915_drv.h"

struct intel_fbdev {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
        struct drm_fb_helper helper;
#endif
	struct intel_framebuffer *fb;
	struct i915_vma *vma;
	unsigned long vma_flags;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
	int preferred_bpp;

	/* Whether or not fbdev hpd processing is temporarily suspended */
	bool hpd_suspended: 1;
	/* Set when a hotplug was received while HPD processing was suspended */
	bool hpd_waiting: 1;

	/* Protects hpd_suspended */
	struct mutex hpd_lock;
#endif
};

static struct intel_fbdev *to_intel_fbdev(struct drm_fb_helper *fb_helper)
{
	struct intel_display *display = to_intel_display(fb_helper->client.dev);

	return display->fbdev.fbdev;
}

static struct intel_frontbuffer *to_frontbuffer(struct intel_fbdev *ifbdev)
{
	return ifbdev->fb->frontbuffer;
}

static void intel_fbdev_invalidate(struct intel_fbdev *ifbdev)
{
	intel_frontbuffer_invalidate(to_frontbuffer(ifbdev), ORIGIN_CPU);
}

FB_GEN_DEFAULT_DEFERRED_IOMEM_OPS(intel_fbdev,
				  drm_fb_helper_damage_range,
				  drm_fb_helper_damage_area)

static int intel_fbdev_set_par(struct fb_info *info)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(info->par);
	int ret;

	ret = drm_fb_helper_set_par(info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_blank(int blank, struct fb_info *info)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(info->par);
	int ret;

	ret = drm_fb_helper_blank(blank, info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_pan_display(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(info->par);
	int ret;

	ret = drm_fb_helper_pan_display(var, info);
	if (ret == 0)
		intel_fbdev_invalidate(ifbdev);

	return ret;
}

static int intel_fbdev_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb_helper->fb, 0);

	return intel_bo_fb_mmap(obj, vma);
}

static void intel_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct intel_fbdev *ifbdev = to_intel_fbdev(fb_helper);

	drm_fb_helper_fini(fb_helper);

	/*
	 * We rely on the object-free to release the VMA pinning for
	 * the info->screen_base mmaping. Leaking the VMA is simpler than
	 * trying to rectify all the possible error paths leading here.
	 */
	intel_fb_unpin_vma(ifbdev->vma, ifbdev->vma_flags);
	drm_framebuffer_remove(fb_helper->fb);

	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field initialization overrides for fb ops");

static const struct fb_ops intelfb_ops = {
	.owner = THIS_MODULE,
	__FB_DEFAULT_DEFERRED_OPS_RDWR(intel_fbdev),
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_set_par = intel_fbdev_set_par,
	.fb_blank = intel_fbdev_blank,
	.fb_pan_display = intel_fbdev_pan_display,
	__FB_DEFAULT_DEFERRED_OPS_DRAW(intel_fbdev),
	.fb_mmap = intel_fbdev_mmap,
	.fb_destroy = intel_fbdev_fb_destroy,
};

__diag_pop();

static int intelfb_dirty(struct drm_fb_helper *helper, struct drm_clip_rect *clip)
{
	if (!(clip->x1 < clip->x2 && clip->y1 < clip->y2))
		return 0;

	if (helper->fb->funcs->dirty)
		return helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, clip, 1);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
static void intelfb_restore(struct drm_fb_helper *fb_helper)
{
	struct intel_fbdev *ifbdev = to_intel_fbdev(fb_helper);

	intel_fbdev_invalidate(ifbdev);
}

static void intelfb_set_suspend(struct drm_fb_helper *fb_helper, bool suspend)
{
	struct fb_info *info = fb_helper->info;

	/*
	 * When resuming from hibernation, Linux restores the object's
	 * content from swap if the buffer is backed by shmemfs. If the
	 * object is stolen however, it will be full of whatever garbage
	 * was left in there. Clear it to zero in this case.
	 */
	if (!suspend && !intel_bo_is_shmem(intel_fb_bo(fb_helper->fb)))
		memset_io(info->screen_base, 0, info->screen_size);

	fb_set_suspend(info, suspend);
}

static const struct drm_fb_helper_funcs intel_fb_helper_funcs = {
	.fb_dirty = intelfb_dirty,
	.fb_restore = intelfb_restore,
	.fb_set_suspend = intelfb_set_suspend,
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
static const struct drm_fb_helper_funcs intel_fb_helper_funcs = {
        .fb_probe = intel_fbdev_driver_fbdev_probe,
	.fb_dirty = intelfb_dirty,
};
#endif

int intel_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes)
{
	struct intel_display *display = to_intel_display(helper->dev);
	struct intel_fbdev *ifbdev = to_intel_fbdev(helper);
	struct intel_framebuffer *fb = ifbdev->fb;
	struct ref_tracker *wakeref;
	struct fb_info *info;
	struct i915_vma *vma;
	unsigned long flags = 0;
	bool prealloc = false;
	struct drm_gem_object *obj;
	int ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
	mutex_lock(&ifbdev->hpd_lock);
	ret = ifbdev->hpd_suspended ? -EAGAIN : 0;
	mutex_unlock(&ifbdev->hpd_lock);
	if (ret)
		return ret;
#endif

	ifbdev->fb = NULL;

	if (fb &&
	    (sizes->fb_width > fb->base.width ||
	     sizes->fb_height > fb->base.height)) {
		drm_dbg_kms(display->drm,
			    "BIOS fb too small (%dx%d), we require (%dx%d),"
			    " releasing it\n",
			    fb->base.width, fb->base.height,
			    sizes->fb_width, sizes->fb_height);
		drm_framebuffer_put(&fb->base);
		fb = NULL;
	}
	if (!fb || drm_WARN_ON(display->drm, !intel_fb_bo(&fb->base))) {
		drm_dbg_kms(display->drm,
			    "no BIOS fb, allocating a new one\n");
		fb = intel_fbdev_fb_alloc(helper, sizes);
		if (IS_ERR(fb))
			return PTR_ERR(fb);
	} else {
		drm_dbg_kms(display->drm, "re-using BIOS fb\n");
		prealloc = true;
		sizes->fb_width = fb->base.width;
		sizes->fb_height = fb->base.height;
	}

	wakeref = intel_display_rpm_get(display);

	/* Pin the GGTT vma for our access via info->screen_base.
	 * This also validates that any existing fb inherited from the
	 * BIOS is suitable for own access.
	 */
	vma = intel_fb_pin_to_ggtt(&fb->base, &fb->normal_view.gtt,
				   fb->min_alignment, 0,
				   intel_fb_view_vtd_guard(&fb->base, &fb->normal_view,
							   DRM_MODE_ROTATE_0),
				   false, &flags);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out_unlock;
	}

	info = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(info)) {
		drm_err(display->drm, "Failed to allocate fb_info (%pe)\n", info);
		ret = PTR_ERR(info);
		goto out_unpin;
	}

	helper->funcs = &intel_fb_helper_funcs;
	helper->fb = &fb->base;

	info->fbops = &intelfb_ops;

	obj = intel_fb_bo(&fb->base);

	ret = intel_fbdev_fb_fill_info(display, info, obj, vma);
	if (ret)
		goto out_unpin;

	drm_fb_helper_fill_info(info, display->drm->fb_helper, sizes);

	/* If the object is shmemfs backed, it will have given us zeroed pages.
	 * If the object is stolen however, it will be full of whatever
	 * garbage was left in there.
	 */
	if (!intel_bo_is_shmem(obj) && !prealloc)
		memset_io(info->screen_base, 0, info->screen_size);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	drm_dbg_kms(display->drm, "allocated %dx%d fb: 0x%08x\n",
		    fb->base.width, fb->base.height,
		    i915_ggtt_offset(vma));
	ifbdev->fb = fb;
	ifbdev->vma = vma;
	ifbdev->vma_flags = flags;

	intel_display_rpm_put(display, wakeref);

	return 0;

out_unpin:
	intel_fb_unpin_vma(vma, flags);
out_unlock:
	intel_display_rpm_put(display, wakeref);

	return ret;
}

/*
 * Build an intel_fbdev struct using a BIOS allocated framebuffer, if possible.
 * The core display code will have read out the current plane configuration,
 * so we use that to figure out if there's an object for us to use as the
 * fb, and if so, we re-use it for the fbdev configuration.
 *
 * Note we only support a single fb shared across pipes for boot (mostly for
 * fbcon), so we just find the biggest and use that.
 */
static bool intel_fbdev_init_bios(struct intel_display *display,
				  struct intel_fbdev *ifbdev)
{
	struct intel_framebuffer *fb = NULL;
	struct intel_crtc *crtc;
	unsigned int max_size = 0;

	/* Find the largest fb */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct drm_gem_object *obj = intel_fb_bo(plane_state->uapi.fb);

		if (!crtc_state->uapi.active) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] not active, skipping\n",
				    crtc->base.base.id, crtc->base.name);
			continue;
		}

		if (!obj) {
			drm_dbg_kms(display->drm,
				    "[PLANE:%d:%s] no fb, skipping\n",
				    plane->base.base.id, plane->base.name);
			continue;
		}

		if (obj->size > max_size) {
			drm_dbg_kms(display->drm,
				    "found possible fb from [PLANE:%d:%s]\n",
				    plane->base.base.id, plane->base.name);
			fb = to_intel_framebuffer(plane_state->uapi.fb);
			max_size = obj->size;
		}
	}

	if (!fb) {
		drm_dbg_kms(display->drm,
			    "no active fbs found, not using BIOS config\n");
		goto out;
	}

	/* Now make sure all the pipes will fit into it */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		unsigned int cur_size;

		if (!crtc_state->uapi.active) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] not active, skipping\n",
				    crtc->base.base.id, crtc->base.name);
			continue;
		}

		drm_dbg_kms(display->drm, "checking [PLANE:%d:%s] for BIOS fb\n",
			    plane->base.base.id, plane->base.name);

		/*
		 * See if the plane fb we found above will fit on this
		 * pipe.  Note we need to use the selected fb's pitch and bpp
		 * rather than the current pipe's, since they differ.
		 */
		cur_size = crtc_state->uapi.adjusted_mode.crtc_hdisplay;
		cur_size = cur_size * fb->base.format->cpp[0];
		if (fb->base.pitches[0] < cur_size) {
			drm_dbg_kms(display->drm,
				    "fb not wide enough for [PLANE:%d:%s] (%d vs %d)\n",
				    plane->base.base.id, plane->base.name,
				    cur_size, fb->base.pitches[0]);
			fb = NULL;
			break;
		}

		cur_size = crtc_state->uapi.adjusted_mode.crtc_vdisplay;
		cur_size = intel_fb_align_height(&fb->base, 0, cur_size);
		cur_size *= fb->base.pitches[0];
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] area: %dx%d, bpp: %d, size: %d\n",
			    crtc->base.base.id, crtc->base.name,
			    crtc_state->uapi.adjusted_mode.crtc_hdisplay,
			    crtc_state->uapi.adjusted_mode.crtc_vdisplay,
			    fb->base.format->cpp[0] * 8,
			    cur_size);

		if (cur_size > max_size) {
			drm_dbg_kms(display->drm,
				    "fb not big enough for [PLANE:%d:%s] (%d vs %d)\n",
				    plane->base.base.id, plane->base.name,
				    cur_size, max_size);
			fb = NULL;
			break;
		}

		drm_dbg_kms(display->drm,
			    "fb big enough [PLANE:%d:%s] (%d >= %d)\n",
			    plane->base.base.id, plane->base.name,
			    max_size, cur_size);
	}

	if (!fb) {
		drm_dbg_kms(display->drm,
			    "BIOS fb not suitable for all pipes, not using\n");
		goto out;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
        ifbdev->preferred_bpp = fb->base.format->cpp[0] * 8;
#endif
	ifbdev->fb = fb;

	drm_framebuffer_get(&ifbdev->fb->base);

	/* Final pass to check if any active pipes don't have fbs */
	for_each_intel_crtc(display->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (!crtc_state->uapi.active)
			continue;

		drm_WARN(display->drm, !plane_state->uapi.fb,
			 "re-used BIOS config but lost an fb on [PLANE:%d:%s]\n",
			 plane->base.base.id, plane->base.name);
	}


	drm_dbg_kms(display->drm, "using BIOS fb for initial console\n");
	return true;

out:

	return false;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
static void intel_fbdev_suspend_worker(struct work_struct *work)
{
	intel_fbdev_set_suspend(container_of(work,
					      struct intel_display,
					      fbdev.suspend_work)->drm,
				FBINFO_STATE_RUNNING,
				true);
}

/* Suspends/resumes fbdev processing of incoming HPD events. When resuming HPD
 * processing, fbdev will perform a full connector reprobe if a hotplug event
 * was received while HPD was suspended.
 */
static void intel_fbdev_hpd_set_suspend(struct intel_display *display, int state)
{
	struct intel_fbdev *ifbdev = display->fbdev.fbdev;
	bool send_hpd = false;

	mutex_lock(&ifbdev->hpd_lock);
	ifbdev->hpd_suspended = state == FBINFO_STATE_SUSPENDED;
	send_hpd = !ifbdev->hpd_suspended && ifbdev->hpd_waiting;
	ifbdev->hpd_waiting = false;
	mutex_unlock(&ifbdev->hpd_lock);

	if (send_hpd) {
		drm_dbg_kms(display->drm, "Handling delayed fbcon HPD event\n");
		drm_fb_helper_hotplug_event(&ifbdev->helper);
	}
}

void intel_fbdev_set_suspend(struct drm_device *dev, int state, bool synchronous)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_fbdev *ifbdev = dev_priv->display->fbdev.fbdev;
	struct fb_info *info;

	if (!ifbdev)
		return;

	if (drm_WARN_ON(dev_priv->display->drm, !HAS_DISPLAY(dev_priv->display)))
		return;

	if (!ifbdev->vma)
		goto set_suspend;

	info = ifbdev->helper.info;

	if (synchronous) {
		/* Flush any pending work to turn the console on, and then
		 * wait to turn it off. It must be synchronous as we are
		 * about to suspend or unload the driver.
		 *
		 * Note that from within the work-handler, we cannot flush
		 * ourselves, so only flush outstanding work upon suspend!
		 */
		if (state != FBINFO_STATE_RUNNING)
			flush_work(&dev_priv->display->fbdev.suspend_work);

		console_lock();
	} else {
		/*
		 * The console lock can be pretty contented on resume due
		 * to all the printk activity.  Try to keep it out of the hot
		 * path of resume if possible.
		 */
		drm_WARN_ON(dev, state != FBINFO_STATE_RUNNING);
		if (!console_trylock()) {
			/* Don't block our own workqueue as this can
			 * be run in parallel with other i915.ko tasks.
			 */
			queue_work(dev_priv->unordered_wq,
				   &dev_priv->display->fbdev.suspend_work);
			return;
		}
	}

	/* On resume from hibernation: If the object is shmemfs backed, it has
	 * been restored from swap. If the object is stolen however, it will be
	 * full of whatever garbage was left in there.
	 */
	if (state == FBINFO_STATE_RUNNING &&
	    !intel_bo_is_shmem(intel_fb_bo(&ifbdev->fb->base)))
		memset_io(info->screen_base, 0, info->screen_size);

	drm_fb_helper_set_suspend(&ifbdev->helper, state);
	console_unlock();

set_suspend:
	intel_fbdev_hpd_set_suspend(dev_priv->display, state);
}

static int intel_fbdev_output_poll_changed(struct drm_device *dev)
{
	struct intel_fbdev *ifbdev = to_i915(dev)->display->fbdev.fbdev;
	bool send_hpd;

	if (!ifbdev)
		return -EINVAL;

	mutex_lock(&ifbdev->hpd_lock);
	send_hpd = !ifbdev->hpd_suspended;
	ifbdev->hpd_waiting = true;
	mutex_unlock(&ifbdev->hpd_lock);

	if (send_hpd && (ifbdev->vma || ifbdev->helper.deferred_setup))
		drm_fb_helper_hotplug_event(&ifbdev->helper);

	return 0;
}

static int intel_fbdev_restore_mode(struct drm_i915_private *dev_priv)
{
	struct intel_fbdev *ifbdev = dev_priv->display->fbdev.fbdev;
	int ret;

	if (!ifbdev)
		return -EINVAL;

	if (!ifbdev->vma)
		return -ENOMEM;

	ret = drm_fb_helper_restore_fbdev_mode_unlocked(&ifbdev->helper);
	if (ret)
		return ret;

	intel_fbdev_invalidate(ifbdev);

	return 0;
}

/*
 * Fbdev client and struct drm_client_funcs
 */

static void intel_fbdev_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = fb_helper->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (fb_helper->info) {
		vga_switcheroo_client_fb_set(pdev, NULL);
		drm_fb_helper_unregister_info(fb_helper);
	} else {
		drm_fb_helper_unprepare(fb_helper);
		drm_client_release(&fb_helper->client);
		kfree(fb_helper);
	}
}

static int intel_fbdev_client_restore(struct drm_client_dev *client)
{
	struct drm_i915_private *dev_priv = to_i915(client->dev);
	int ret;

	ret = intel_fbdev_restore_mode(dev_priv);
	if (ret)
		return ret;

	vga_switcheroo_process_delayed_switch();

	return 0;
}

static int intel_fbdev_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int ret;

	if (dev->fb_helper)
		return intel_fbdev_output_poll_changed(dev);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto err_drm_err;

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto err_drm_fb_helper_fini;

	vga_switcheroo_client_fb_set(pdev, fb_helper->info);

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_drm_err:
	drm_err(dev, "Failed to setup i915 fbdev emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs intel_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= intel_fbdev_client_unregister,
	.restore	= intel_fbdev_client_restore,
	.hotplug	= intel_fbdev_client_hotplug,
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
static unsigned int intel_fbdev_color_mode(const struct drm_format_info *info)
{
	unsigned int bpp;

	if (!info->depth || info->num_planes != 1 || info->has_alpha || info->is_yuv)
		return 0;

	bpp = drm_format_info_bpp(info, 0);

	switch (bpp) {
	case 16:
		return info->depth; // 15 or 16
	default:
		return bpp;
	}
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
void intel_fbdev_setup(struct intel_display *display)
{
	struct drm_device *dev = display->drm;
	struct intel_fbdev *ifbdev;
	int ret;

	if (!HAS_DISPLAY(display))
		return;

	ifbdev = kzalloc(sizeof(*ifbdev), GFP_KERNEL);
	if (!ifbdev)
		return;
	drm_fb_helper_prepare(dev, &ifbdev->helper, 32, &intel_fb_helper_funcs);

	display->fbdev.fbdev = ifbdev;
	INIT_WORK(&display->fbdev.suspend_work, intel_fbdev_suspend_worker);
	mutex_init(&ifbdev->hpd_lock);
	if (intel_fbdev_init_bios(display, ifbdev))
		ifbdev->helper.preferred_bpp = ifbdev->preferred_bpp;
	else
		ifbdev->preferred_bpp = ifbdev->helper.preferred_bpp;

	ret = drm_client_init(dev, &ifbdev->helper.client, "intel-fbdev",
			      &intel_fbdev_client_funcs);
	if (ret) {
		drm_err(dev, "Failed to register client: %d\n", ret);
		goto err_drm_fb_helper_unprepare;
	}

	drm_client_register(&ifbdev->helper.client);

	return;

err_drm_fb_helper_unprepare:
	drm_fb_helper_unprepare(&ifbdev->helper);
	mutex_destroy(&ifbdev->hpd_lock);
	kfree(ifbdev);
}
#else
void intel_fbdev_setup(struct intel_display *display)
{
	struct intel_fbdev *ifbdev;
	unsigned int preferred_bpp = 0;

	if (!HAS_DISPLAY(display))
		return;

	ifbdev = drmm_kzalloc(display->drm, sizeof(*ifbdev), GFP_KERNEL);
	if (!ifbdev)
		return;

	display->fbdev.fbdev = ifbdev;
	if (intel_fbdev_init_bios(display, ifbdev))
		preferred_bpp = intel_fbdev_color_mode(ifbdev->fb->base.format);
	if (!preferred_bpp)
		preferred_bpp = 32;

	drm_client_setup_with_color_mode(display->drm, preferred_bpp);
}
#endif

struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev)
{
	if (!fbdev)
		return NULL;

	return fbdev->fb;
}

struct i915_vma *intel_fbdev_vma_pointer(struct intel_fbdev *fbdev)
{
	return fbdev ? fbdev->vma : NULL;
}

void intel_fbdev_get_map(struct intel_fbdev *fbdev, struct iosys_map *map)
{
	intel_fb_get_map(fbdev->vma, map);
}
