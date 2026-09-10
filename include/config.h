#include <linux/version.h>

#ifndef __ASSEMBLY__
#define LINUX_BACKPORT(__sym) intel_drm_shim_##__sym
#endif

#define MODULE_ABS_PATH(path) DKMS_MODULE_SOURCE_DIR/path

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

// Upstream renamed struct drm_atomic_state -> struct drm_atomic_commit
// (and its alloc/get/put/clear/init helpers) starting with kernel 7.2.
// Alias the new names back to the pre-rename ones on older kernels.
// Safe despite the pre-existing drm_atomic_commit() DRM core function
// (a different, unrelated commit-submission call): this header is force
// -included before every other header (see LINUXINCLUDE in Makefile), so
// the substitution applies uniformly to the kernel's own declaration of
// drm_atomic_commit() too -- it becomes a function literally named
// drm_atomic_state, which is legal C (function names and struct tags are
// separate namespaces) and keeps every call site consistent.
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
