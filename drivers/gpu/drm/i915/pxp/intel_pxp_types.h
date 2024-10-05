/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TYPES_H__
#define __INTEL_PXP_TYPES_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct drm_i915_private;

struct intel_context;
struct intel_gt;
struct i915_pxp_component;
struct i915_vma;

#define INTEL_PXP_MAX_HWDRM_SESSIONS 16

struct intel_pxp_session {
	/** @index: Numeric identifier for this protected session */
	int index;
	/** @protection_type: type of protection requested */
	int protection_type;
	/** @protection_mode: mode of protection requested */
	int protection_mode;
	/** @drmfile: pointer to drm_file, which is allocated on device file open() call */
	struct drm_file *drmfile;

	/**
	 * @is_valid: indicates whether the session has been established
	 *            in the HW root of trust. Note that, after a teardown, the
	 *            session can still be considered in play on the HW even if
	 *            the keys are gone, so we can't rely on the HW state of the
	 *            session to know if it's valid.
	 */
	bool is_valid;

	u32 tag;
};

/**
 * struct intel_pxp - pxp state
 */
struct intel_pxp {
	/**
	 * @ctrl_gt: poiner to the tile that owns the controls for PXP subsystem assets that
	 * the VDBOX, the KCR engine (and GSC CS depending on the platform)
	 */
	struct intel_gt *ctrl_gt;

	/**
	 * @platform_cfg_is_bad: used to track if any prior arb session creation resulted
	 * in a failure that was caused by a platform configuration issue, meaning that
	 * failure will not get resolved without a change to the platform (not kernel)
	 * such as BIOS configuration, firwmware update, etc. This bool gets reflected when
	 * GET_PARAM:I915_PARAM_PXP_STATUS is called.
	 */
	bool platform_cfg_is_bad;

	/**
	 * @kcr_base: base mmio offset for the KCR engine which is different on legacy platforms
	 * vs newer platforms where the KCR is inside the media-tile.
	 */
	u32 kcr_base;

	/**
	 * @gsccs_res: resources for request submission for platforms that have a GSC engine.
	 */
	struct gsccs_session_resources {
		u64 host_session_handle; /* used by firmware to link commands to sessions */
		struct intel_context *ce; /* context for gsc command submission */
		struct i915_address_space *vm; /* only for user space session contexts */

		struct i915_vma *pkt_vma; /* GSC FW cmd packet vma */
		void *pkt_vaddr;  /* GSC FW cmd packet virt pointer */

		struct i915_vma *bb_vma; /* HECI_PKT batch buffer vma */
		void *bb_vaddr; /* HECI_PKT batch buffer virt pointer */
	} gsccs_res;
	/** @gsccs_clients: list of gsccs_res structs for each active client. */
	struct list_head gsccs_clients; /* protected by session_mutex */

	/**
	 * @pxp_component: i915_pxp_component struct of the bound mei_pxp
	 * module. Only set and cleared inside component bind/unbind functions,
	 * which are protected by &tee_mutex.
	 */
	struct i915_pxp_component *pxp_component;

	/**
	 * @dev_link: Enforce module relationship for power management ordering.
	 */
	struct device_link *dev_link;
	/**
	 * @mei_pxp_last_msg_interrupted: To catch and drop stale responses
	 * from previuosly interrupted send-msg to mei before issuing new
	 * send-recv.
	 */
	bool mei_pxp_last_msg_interrupted;

	/**
	 * @pxp_component_added: track if the pxp component has been added.
	 * Set and cleared in tee init and fini functions respectively.
	 */
	bool pxp_component_added;

	/** @ce: kernel-owned context used for PXP operations */
	struct intel_context *ce;

	/** @arb_mutex: protects arb session start */
	struct mutex arb_mutex;

	/**
	 * @key_instance: tracks which key instance we're on, so we can use it
	 * to determine if an object was created using the current key or a
	 * previous one.
	 */
	u32 key_instance;

	/** @tee_mutex: protects the tee channel binding and messaging. */
	struct mutex tee_mutex;

	/** @stream_cmd: LMEM obj used to send stream PXP commands to the GSC */
	struct {
		struct drm_i915_gem_object *obj; /* contains PXP command memory */
		void *vaddr; /* virtual memory for PXP command */
	} stream_cmd;

	/**
	 * @hw_state_invalidated: if the HW perceives an attack on the integrity
	 * of the encryption it will invalidate the keys and expect SW to
	 * re-initialize the session. We keep track of this state to make sure
	 * we only re-start the arb session when required.
	 */
	bool hw_state_invalidated;

	/** @irq_enabled: tracks the status of the kcr irqs */
	bool irq_enabled;
	/**
	 * @termination: tracks the status of a pending termination. Only
	 * re-initialized under gt->irq_lock and completed in &session_work.
	 */
	struct completion termination;

	/** @session_mutex: protects hwdrm_sesions, and reserved_sessions. */
	struct mutex session_mutex;
	/** @reserved_sessions: bitmap of hw session slots for used-vs-free book-keeping. */
	DECLARE_BITMAP(reserved_sessions, INTEL_PXP_MAX_HWDRM_SESSIONS);
	/** @hwdrm_sessions: array of intel_pxp_sesion ptrs mapped to reserved_sessions bitmap. */
	struct intel_pxp_session *hwdrm_sessions[INTEL_PXP_MAX_HWDRM_SESSIONS];
	/** @arb_session: the default intel_pxp_session. */
	struct intel_pxp_session arb_session;
	/** @next_tag_id: looping counter (per session) to track teardown-creation events. */
	u8 next_tag_id[INTEL_PXP_MAX_HWDRM_SESSIONS];

	/** @session_work: worker that manages session events. */
	struct work_struct session_work;
	/** @session_events: pending session events, protected with gt->irq_lock. */
	u32 session_events;
#define PXP_TERMINATION_REQUEST  BIT(0)
#define PXP_TERMINATION_COMPLETE BIT(1)
#define PXP_INVAL_REQUIRED       BIT(2)
};

#endif /* __INTEL_PXP_TYPES_H__ */
