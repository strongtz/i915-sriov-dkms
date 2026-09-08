/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright (c) 2026
 *
 * Backport functionality for older kernels
 *
 * Upstream reworked drm_exec_until_all_locked()/drm_exec_retry_on_contention()
 * from a computed-goto (&&label) implementation to a plain named-label one,
 * and added a new public drm_exec_retry() macro (plus the DRM_EXEC_DUMMY
 * sentinel it needs, previously private to drm_exec.c) starting with kernel
 * 7.2. xe_validation.h uses drm_exec_retry() unconditionally, so older
 * kernels need it backported. The named-label style isn't gated on any real
 * kernel capability -- it's just an implementation choice -- so we simply
 * redefine the three macros to the upstream 7.2 versions here.
 *
 * This header is force-included ahead of every xe source file (see
 * drivers/gpu/drm/xe/Makefile), so its own #include of <drm/drm_exec.h>
 * establishes that header's include guard first; the driver's own later
 * #include <drm/drm_exec.h> then becomes a no-op and our redefinitions
 * below stick for the rest of the translation unit.
 */

#ifndef _I915_SRIOV_COMPAT_DRM_EXEC_H_
#define _I915_SRIOV_COMPAT_DRM_EXEC_H_

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0)

#include <drm/drm_exec.h>

#ifndef DRM_EXEC_DUMMY
#define DRM_EXEC_DUMMY ((void *)~0)
#endif

#undef drm_exec_until_all_locked
#undef drm_exec_retry_on_contention
#undef drm_exec_retry

#define drm_exec_until_all_locked(exec)					\
	for (bool const __maybe_unused __drm_exec_loop = false;		\
	     drm_exec_cleanup(exec);)					\
		if (false) {						\
drm_exec_retry: __maybe_unused;						\
			continue;					\
		} else

#define drm_exec_retry_on_contention(exec)			\
	do {							\
		if (unlikely(drm_exec_is_contended(exec)))	\
			goto drm_exec_retry;			\
	} while (__drm_exec_loop)

#define drm_exec_retry(_exec)					\
	do {							\
		WARN_ON((_exec)->contended != DRM_EXEC_DUMMY);	\
		goto drm_exec_retry;				\
	} while (__drm_exec_loop)

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0) */

#endif /* _I915_SRIOV_COMPAT_DRM_EXEC_H_ */
