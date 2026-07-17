#include <linux/version.h>

#ifndef __ASSEMBLY__
#define LINUX_BACKPORT(__sym) intel_drm_shim_##__sym
#endif

#define MODULE_ABS_PATH(path) DKMS_MODULE_SOURCE_DIR/path

/*
 * Linux 7.1: the buddy allocator was split out of the DRM subsystem and moved
 * to <linux/gpu_buddy.h> as gpu_buddy_*. Only the two DRM print helpers
 * (drm_buddy_print, drm_buddy_block_print) keep their names in
 * <drm/drm_buddy.h>. The i915 tree vendored from 7.0 only knows the old
 * drm_buddy_* names — remapped globally here so that struct FIELDS
 * (struct drm_buddy *mm;) in early-included i915 headers are covered too, not
 * just the later use sites. That is why this lives in the -include config and
 * not in a header shim (include-ordering trap).
 * Macro replacement is token-based: drm_buddy_print stays untouched.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
#define drm_buddy                       gpu_buddy
#define drm_buddy_block                 gpu_buddy_block
#define drm_buddy_block_size            gpu_buddy_block_size
#define drm_buddy_block_offset          gpu_buddy_block_offset
#define drm_buddy_block_order           gpu_buddy_block_order
#define drm_buddy_block_is_free         gpu_buddy_block_is_free
#define drm_buddy_block_is_clear        gpu_buddy_block_is_clear
#define drm_buddy_block_trim            gpu_buddy_block_trim
#define drm_buddy_free_block            gpu_buddy_free_block
#define drm_buddy_free_list             gpu_buddy_free_list
#define drm_buddy_alloc_blocks          gpu_buddy_alloc_blocks
#define drm_buddy_init                  gpu_buddy_init
#define drm_buddy_fini                  gpu_buddy_fini
#define DRM_BUDDY_RANGE_ALLOCATION      GPU_BUDDY_RANGE_ALLOCATION
#define DRM_BUDDY_TOPDOWN_ALLOCATION    GPU_BUDDY_TOPDOWN_ALLOCATION
#define DRM_BUDDY_CONTIGUOUS_ALLOCATION GPU_BUDDY_CONTIGUOUS_ALLOCATION
#define DRM_BUDDY_CLEAR_ALLOCATION      GPU_BUDDY_CLEAR_ALLOCATION
#define DRM_BUDDY_CLEARED               GPU_BUDDY_CLEARED
#endif

/*
 * Linux 7.1: INTEL_GMCH_CTRL (PCI config offset 0x52 of the GMCH control
 * register) was renamed to I830_GMCH_CTRL in <drm/intel/i915_drm.h> — same
 * value (0x52). INTEL_GMCH_VGA_DISABLE is unchanged. Pure rename with identical
 * semantics -> global #define (class 2), keeping the intel_vga.c vendored from
 * 7.0 unchanged. Token-based: I830_GMCH_CTRL/SNB_GMCH_CTRL stay untouched.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
#define INTEL_GMCH_CTRL I830_GMCH_CTRL
#endif

/*
 * Linux 7.1 ("dynamic dma-buf" rework): the exported function
 * dma_buf_move_notify(struct dma_buf *) was renamed to
 * dma_buf_invalidate_mappings() (same signature/semantics: notify importers of
 * a move). Unique token -> global #define (class 2). The corresponding callback
 * FIELD dma_buf_attach_ops.move_notify -> .invalidate_mappings is NOT mapped
 * here (move_notify is a common name); it is handled locally in xe_dma_buf.c.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
#define dma_buf_move_notify dma_buf_invalidate_mappings
#endif

// We vendor our own copy of the DRM_GPUSVM module, so enable it here.

#ifndef CONFIG_HMM_MIRROR 
#error "CONFIG_HMM_MIRROR is required for DRM_GPUSVM"
#endif

#ifndef CONFIG_MMU_NOTIFIER
#error "CONFIG_MMU_NOTIFIER is required for DRM_GPUSVM"
#endif

#ifdef CONFIG_DRM_GPUSVM
#undef CONFIG_DRM_GPUSVM
#endif
#ifdef CONFIG_DRM_GPUSVM_MODULE
#undef CONFIG_DRM_GPUSVM_MODULE
#endif
#define CONFIG_DRM_GPUSVM_MODULE 1

// Enable i915 driver and its features by default.
// The following values are based on the Arch Linux kernel config.
#ifdef CONFIG_DRM_I915
#undef CONFIG_DRM_I915
#endif
#ifndef CONFIG_DRM_I915_MODULE
#define CONFIG_DRM_I915_MODULE 1
#endif

#ifndef CONFIG_DRM_I915_CAPTURE_ERROR
#define CONFIG_DRM_I915_CAPTURE_ERROR 1
#endif

#ifndef CONFIG_DRM_I915_COMPRESS_ERROR
#define CONFIG_DRM_I915_COMPRESS_ERROR 1
#endif

#ifndef CONFIG_DRM_I915_DP_TUNNEL
#define CONFIG_DRM_I915_DP_TUNNEL 1
#endif

#ifndef CONFIG_DRM_I915_FENCE_TIMEOUT
#define CONFIG_DRM_I915_FENCE_TIMEOUT 10000
#endif

#ifndef CONFIG_DRM_I915_FORCE_PROBE
#define CONFIG_DRM_I915_FORCE_PROBE "*"
#endif

#ifndef CONFIG_DRM_I915_GVT
#define CONFIG_DRM_I915_GVT 1
#endif

#ifndef CONFIG_DRM_I915_GVT_KVMGT_MODULE
#define CONFIG_DRM_I915_GVT_KVMGT_MODULE 1
#endif

#ifndef CONFIG_DRM_I915_HEARTBEAT_INTERVAL
#define CONFIG_DRM_I915_HEARTBEAT_INTERVAL 2500
#endif

#ifndef CONFIG_DRM_I915_MAX_REQUEST_BUSYWAIT
#define CONFIG_DRM_I915_MAX_REQUEST_BUSYWAIT 8000
#endif

#ifndef CONFIG_DRM_I915_PREEMPT_TIMEOUT
#define CONFIG_DRM_I915_PREEMPT_TIMEOUT 640
#endif

#ifndef CONFIG_DRM_I915_PREEMPT_TIMEOUT_COMPUTE
#define CONFIG_DRM_I915_PREEMPT_TIMEOUT_COMPUTE 7500
#endif

#ifndef CONFIG_DRM_I915_PXP
#define CONFIG_DRM_I915_PXP 1
#endif

#ifndef CONFIG_DRM_I915_REQUEST_TIMEOUT
#define CONFIG_DRM_I915_REQUEST_TIMEOUT 20000
#endif

#ifndef CONFIG_DRM_I915_STOP_TIMEOUT
#define CONFIG_DRM_I915_STOP_TIMEOUT 100
#endif

#ifndef CONFIG_DRM_I915_TIMESLICE_DURATION
#define CONFIG_DRM_I915_TIMESLICE_DURATION 1
#endif

#ifndef CONFIG_DRM_I915_USERFAULT_AUTOSUSPEND
#define CONFIG_DRM_I915_USERFAULT_AUTOSUSPEND 250
#endif

#ifndef CONFIG_DRM_I915_USERPTR
#define CONFIG_DRM_I915_USERPTR 1
#endif

// Enable xe driver and its features by default.
// The following values are based on the Arch Linux kernel config.
#ifdef CONFIG_DRM_XE
#undef CONFIG_DRM_XE
#endif
#ifndef CONFIG_DRM_XE_MODULE
#define CONFIG_DRM_XE_MODULE 1
#endif

#ifndef CONFIG_DRM_XE_DISPLAY
#define CONFIG_DRM_XE_DISPLAY 1
#endif

#ifndef CONFIG_DRM_XE_DP_TUNNEL
#define CONFIG_DRM_XE_DP_TUNNEL 1
#endif

#ifndef CONFIG_DRM_XE_ENABLE_SCHEDTIMEOUT_LIMIT
#define CONFIG_DRM_XE_ENABLE_SCHEDTIMEOUT_LIMIT 1
#endif

#ifndef CONFIG_DRM_XE_FORCE_PROBE
#define CONFIG_DRM_XE_FORCE_PROBE ""
#endif

#ifndef CONFIG_DRM_XE_GPUSVM
#define CONFIG_DRM_XE_GPUSVM 1
#endif

#ifndef CONFIG_DRM_XE_JOB_TIMEOUT_MAX
#define CONFIG_DRM_XE_JOB_TIMEOUT_MAX 10000
#endif

#ifndef CONFIG_DRM_XE_JOB_TIMEOUT_MIN
#define CONFIG_DRM_XE_JOB_TIMEOUT_MIN 1
#endif

#ifndef CONFIG_DRM_XE_PAGEMAP
#ifdef CONFIG_GET_FREE_REGION
#define CONFIG_DRM_XE_PAGEMAP 1
#endif
#endif

#ifndef CONFIG_DRM_XE_PREEMPT_TIMEOUT
#define CONFIG_DRM_XE_PREEMPT_TIMEOUT 640000
#endif

#ifndef CONFIG_DRM_XE_PREEMPT_TIMEOUT_MAX
#define CONFIG_DRM_XE_PREEMPT_TIMEOUT_MAX 10000000
#endif

#ifndef CONFIG_DRM_XE_PREEMPT_TIMEOUT_MIN
#define CONFIG_DRM_XE_PREEMPT_TIMEOUT_MIN 1
#endif

#ifndef CONFIG_DRM_XE_TIMESLICE_MAX
#define CONFIG_DRM_XE_TIMESLICE_MAX 10000000
#endif

#ifndef CONFIG_DRM_XE_TIMESLICE_MIN
#define CONFIG_DRM_XE_TIMESLICE_MIN 1
#endif
