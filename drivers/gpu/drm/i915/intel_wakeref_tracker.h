/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_WAKEREF_TRACKER_H
#define INTEL_WAKEREF_TRACKER_H

#include <linux/kconfig.h>
#include <linux/spinlock.h>
#include <linux/stackdepot.h>

typedef depot_stack_handle_t intel_wakeref_t;

struct drm_printer;

struct intel_wakeref_tracker {
	spinlock_t lock;

	depot_stack_handle_t last_acquire;
	depot_stack_handle_t last_release;

	depot_stack_handle_t *owners;
	unsigned long count;
};

#if IS_ENABLED(CONFIG_DRM_I915_TRACK_WAKEREF)

void intel_wakeref_tracker_init(struct intel_wakeref_tracker *w);
void intel_wakeref_tracker_fini(struct intel_wakeref_tracker *w);

intel_wakeref_t intel_wakeref_tracker_add(struct intel_wakeref_tracker *w);
void intel_wakeref_tracker_remove(struct intel_wakeref_tracker *w,
			   intel_wakeref_t handle);

struct intel_wakeref_tracker
__intel_wakeref_tracker_reset(struct intel_wakeref_tracker *w);
void intel_wakeref_tracker_reset(struct intel_wakeref_tracker *w,
				 struct drm_printer *p);

void __intel_wakeref_tracker_show(const struct intel_wakeref_tracker *w,
				  struct drm_printer *p);
void intel_wakeref_tracker_show(struct intel_wakeref_tracker *w,
				struct drm_printer *p);

#else

static inline void intel_wakeref_tracker_init(struct intel_wakeref_tracker *w) {}
static inline void intel_wakeref_tracker_fini(struct intel_wakeref_tracker *w) {}

static inline intel_wakeref_t
intel_wakeref_tracker_add(struct intel_wakeref_tracker *w)
{
	return -1;
}

static inline void
intel_wakeref_untrack_remove(struct intel_wakeref_tracker *w, intel_wakeref_t handle) {}

static inline struct intel_wakeref_tracker
__intel_wakeref_tracker_reset(struct intel_wakeref_tracker *w)
{
	return (struct intel_wakeref_tracker){};
}

static inline void intel_wakeref_tracker_reset(struct intel_wakeref_tracker *w,
					       struct drm_printer *p)
{
}

static inline void __intel_wakeref_tracker_show(const struct intel_wakeref_tracker *w, struct drm_printer *p) {}
static inline void intel_wakeref_tracker_show(struct intel_wakeref_tracker *w, struct drm_printer *p) {}

#endif

#endif /* INTEL_WAKEREF_TRACKER_H */
