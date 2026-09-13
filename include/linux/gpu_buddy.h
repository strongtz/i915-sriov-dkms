#ifndef __BACKPORT_LINUX_GPU_BUDDY_H__
#define __BACKPORT_LINUX_GPU_BUDDY_H__
#if __has_include_next(<linux/gpu_buddy.h>)
#include_next <linux/gpu_buddy.h>
#else
#include <drm/drm_buddy.h>
#define gpu_buddy drm_buddy
#define gpu_buddy_block drm_buddy_block
#define gpu_buddy_block_trim drm_buddy_block_trim
#define gpu_buddy_alloc_blocks drm_buddy_alloc_blocks
#define gpu_buddy_block_offset drm_buddy_block_offset
#define gpu_buddy_block_size drm_buddy_block_size
#define gpu_buddy_free_list drm_buddy_free_list
#define gpu_buddy_init drm_buddy_init
#define gpu_buddy_fini drm_buddy_fini
#define GPU_BUDDY_TOPDOWN_ALLOCATION DRM_BUDDY_TOPDOWN_ALLOCATION
#define GPU_BUDDY_CONTIGUOUS_ALLOCATION DRM_BUDDY_CONTIGUOUS_ALLOCATION
#define GPU_BUDDY_RANGE_ALLOCATION DRM_BUDDY_RANGE_ALLOCATION
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 2, 0)
#ifdef CONFIG_LOCKDEP
/**
 * gpu_buddy_driver_set_lock() - Set the lock protecting accesses to GPU BUDDY
 * @mm: Pointer to GPU buddy structure.
 * @lock: the lock used to protect the gpu buddy. The locking primitive
 * must contain a dep_map field.
 *
 * Call this to annotate gpu_buddy APIs which access/modify gpu_buddy manager
 */
#define gpu_buddy_driver_set_lock(mm, lock) \
	do { \
		struct gpu_buddy *__mm = (mm); \
		if (!WARN(__mm->lock_dep_map, "GPU BUDDY MM lock should be set only once.")) \
			__mm->lock_dep_map = &(lock)->dep_map; \
	} while (0)
#else
#define gpu_buddy_driver_set_lock(mm, lock) do { (void)(mm); (void)(lock); } while (0)
#endif
#endif

#endif