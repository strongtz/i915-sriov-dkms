#ifndef __BACKPORT_LINUX_GPU_BUDDY_H__
#define __BACKPORT_LINUX_GPU_BUDDY_H__
#if __has_include_next(<linux/gpu_buddy.h>)
#include_next <linux/gpu_buddy.h>
#else
#include <drm/drm_buddy.h>
#define GPU_BUDDY_TOPDOWN_ALLOCATION DRM_BUDDY_TOPDOWN_ALLOCATION
#define GPU_BUDDY_CONTIGUOUS_ALLOCATION DRM_BUDDY_CONTIGUOUS_ALLOCATION
#define GPU_BUDDY_RANGE_ALLOCATION DRM_BUDDY_RANGE_ALLOCATION
#define gpu_buddy drm_buddy
#define gpu_buddy_block drm_buddy_block
#define gpu_buddy_block_trim drm_buddy_block_trim
#define gpu_buddy_alloc_blocks drm_buddy_alloc_blocks
#define gpu_buddy_block_offset drm_buddy_block_offset
#define gpu_buddy_block_size drm_buddy_block_size
#define gpu_buddy_free_list drm_buddy_free_list
#define gpu_buddy_init drm_buddy_init
#define gpu_buddy_fini drm_buddy_fini
#endif
#endif
