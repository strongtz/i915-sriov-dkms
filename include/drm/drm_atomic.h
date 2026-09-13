#include_next <drm/drm_atomic.h>

#ifndef __BACKPORT_DRM_ATOMIC_H__
#define __BACKPORT_DRM_ATOMIC_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0)
static inline int __idb_shim_drm_atomic_commit(struct drm_atomic_state *state) {
	return drm_atomic_commit(state);
}
#else
static inline int __idb_shim_drm_atomic_commit(struct drm_atomic_commit *commit) {
	return drm_atomic_commit(commit);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0)
#define drm_atomic_commit                 drm_atomic_state
#define drm_atomic_commit_alloc           drm_atomic_state_alloc
#define drm_atomic_commit_clear           drm_atomic_state_clear
#define drm_atomic_commit_get             drm_atomic_state_get
#define drm_atomic_commit_put             drm_atomic_state_put
#define drm_atomic_commit_init            drm_atomic_state_init
#define drm_atomic_commit_default_clear   drm_atomic_state_default_clear
#define drm_atomic_commit_default_release drm_atomic_state_default_release
#endif

#endif