#include_next <drm/drm_exec.h>
#ifndef __BACKPORT_DRM_EXEC_H__
#define __BACKPORT_DRM_EXEC_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0)
#define DRM_EXEC_DUMMY ((void *)~0)

/**
 * drm_exec_until_all_locked - loop until all GEM objects are locked
 * @exec: drm_exec object
 *
 * Core functionality of the drm_exec object. Loops until all GEM objects are
 * locked and no more contention exists. At the beginning of the loop it is
 * guaranteed that no GEM object is locked.
 *
 * A global label name drm_exec_retry is used, if you need to use more than one
 * instance of this macro in the same function the label needs to be made local
 * to the block with the __label__ keyword.
 */
#undef drm_exec_until_all_locked
#define drm_exec_until_all_locked(exec)					\
	for (bool const __maybe_unused __drm_exec_loop = false;		\
	     drm_exec_cleanup(exec);)					\
		if (false) {						\
drm_exec_retry: __maybe_unused;						\
			continue;					\
		} else

/**
 * drm_exec_retry_on_contention - restart the loop to grap all locks
 * @exec: drm_exec object
 *
 * Control flow helper to continue when a contention was detected and we need to
 * clean up and re-start the loop to prepare all GEM objects.
 * The __drm_exec_loop check exists to prevent usage outside of an
 * drm_exec_until_all_locked() loop.
 */
#undef drm_exec_retry_on_contention
#define drm_exec_retry_on_contention(exec)			\
	do {							\
		if (unlikely(drm_exec_is_contended(exec)))	\
			goto drm_exec_retry;			\
	} while (__drm_exec_loop)


/**
 * drm_exec_retry() - Unconditionally restart the loop to grab all locks.
 * @exec: drm_exec object
 *
 * Unconditionally retry the loop to lock all objects. For consistency,
 * the exec object needs to be newly initialized.
 * The __drm_exec_loop check exists to prevent usage outside of an
 * drm_exec_until_all_locked() loop.
 */
#define drm_exec_retry(_exec)					\
	do {							\
		WARN_ON((_exec)->contended != DRM_EXEC_DUMMY);	\
		goto drm_exec_retry;				\
	} while (__drm_exec_loop)

#endif

#endif