// SPDX-License-Identifier: MIT
/*
 * Copyright © 2017 Intel Corporation
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/string.h>

#include "i915_drv.h"
#include "i915_gemfs.h"
#include "i915_utils.h"

void i915_gemfs_init(struct drm_i915_private *i915)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
	char huge_opt[] = "huge=within_size"; /* r/w */
#endif
	struct file_system_type *type;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
	struct fs_context *fc;
#endif
	struct vfsmount *gemfs;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
	int ret;
#endif

	/*
	 * By creating our own shmemfs mountpoint, we can pass in
	 * mount flags that better match our usecase.
	 *
	 * One example, although it is probably better with a per-file
	 * control, is selecting huge page allocations ("huge=within_size").
	 * However, we only do so on platforms which benefit from it, or to
	 * offset the overhead of iommu lookups, where with latter it is a net
	 * win even on platforms which would otherwise see some performance
	 * regressions such a slow reads issue on Broadwell and Skylake.
	 */

	if (GRAPHICS_VER(i915) < 11 && !i915_vtd_active(i915))
		return;

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		goto err;

	type = get_fs_type("tmpfs");
	if (!type)
		goto err;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
	gemfs = vfs_kern_mount(type, SB_KERNMOUNT, type->name, huge_opt);
	if (IS_ERR(gemfs))
		goto err;
#else
	fc = fs_context_for_mount(type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		goto err;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	ret = vfs_parse_fs_string(fc, "source", "tmpfs", 5);
#else
	ret = vfs_parse_fs_string(fc, "source", "tmpfs");
#endif
	if (!ret)
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
		ret = vfs_parse_fs_string(fc, "huge", "within_size", 11);
#else
		ret = vfs_parse_fs_string(fc, "huge", "within_size");
#endif
	if (!ret)
		gemfs = fc_mount_longterm(fc);
	put_fs_context(fc);
	if (ret)
		goto err;
#endif

	i915->mm.gemfs = gemfs;
	drm_info(&i915->drm, "Using Transparent Hugepages\n");
	return;

err:
	drm_notice(&i915->drm,
		   "Transparent Hugepage support is recommended for optimal performance%s\n",
		   GRAPHICS_VER(i915) >= 11 ? " on this platform!" :
					      " when IOMMU is enabled!");
}

void i915_gemfs_fini(struct drm_i915_private *i915)
{
	kern_unmount(i915->mm.gemfs);
}
