/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_IOV_TYPES_H__
#define __INTEL_IOV_TYPES_H__

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <drm/drm_mm.h>
#include "i915_reg.h"
#include "i915_selftest.h"
#include "intel_wakeref.h"

/*
 * Super macro that combines different names used with adverse events:
 * threshold key name (used in code), friendly name (used in dmesg)
 * and attribute name (used in sysfs):
 *
 *	threshold(Key, Name, Attribute)
 */
#define IOV_THRESHOLDS(threshold) \
	threshold(CAT_ERR, cat_error, cat_error_count) \
	threshold(ENGINE_RESET, engine_reset, engine_reset_count) \
	threshold(PAGE_FAULT, page_fault, page_fault_count) \
	threshold(H2G_STORM, guc_storm, h2g_time_us) \
	threshold(IRQ_STORM, irq_storm, irq_time_us) \
	threshold(DOORBELL_STORM, dbs_storm, doorbell_time_us) \
	/*end*/

enum intel_iov_threshold {
#define __to_intel_iov_threshold_enum(K, ...) IOV_THRESHOLD_##K,
IOV_THRESHOLDS(__to_intel_iov_threshold_enum)
#undef __to_intel_iov_threshold_enum
};

#define __count_iov_thresholds(...) + 1
#define IOV_THRESHOLD_MAX (0 IOV_THRESHOLDS(__count_iov_thresholds))

/**
 * struct intel_iov_config - IOV configuration data.
 * @ggtt_region: GGTT region.
 * @num_ctxs: number of GuC submission contexts.
 * @begin_ctx: start index of GuC contexts.
 * @num_dbs: number of GuC doorbells.
 * @begin_db: start index of GuC doorbells.
 * @exec_quantum: execution-quantum in milliseconds.
 * @preempt_timeout: preemption timeout in microseconds.
 */
struct intel_iov_config {
	struct drm_mm_node ggtt_region;
	u16 num_ctxs;
	u16 begin_ctx;
	u16 num_dbs;
	u16 begin_db;
	u32 exec_quantum;
	u32 preempt_timeout;
	u32 thresholds[IOV_THRESHOLD_MAX];
};

/**
 * struct intel_iov_sysfs - IOV sysfs data.
 * @entries: array with kobjects that represent PF and VFs.
 */
struct intel_iov_sysfs {
	struct kobject **entries;
};

/**
 * struct intel_iov_policies - IOV policies.
 * @sched_if_idle: controls strict scheduling.
 * @reset_engine: controls engines reset on VF switch.
 * @sample_period: sample period of adverse events in milliseconds.
 */
struct intel_iov_policies {
	bool sched_if_idle;
	bool reset_engine;
	u32 sample_period;
};

/**
 * struct intel_iov_provisioning - IOV provisioning data.
 * @auto_mode: indicates manual or automatic provisioning mode.
 * @policies: provisioning policies.
 * @configs: flexible array with configuration data for PF and VFs.
 * @lock: protects provisionining data
 */
struct intel_iov_provisioning {
	bool auto_mode;
	unsigned int num_pushed;
	struct work_struct worker;
	struct intel_iov_policies policies;
	struct intel_iov_config *configs;
	struct mutex lock;

	I915_SELFTEST_DECLARE(bool self_done);
};

#define VFID(n)		(n)
#define PFID		VFID(0)

/**
 * struct intel_iov_data - Data related to one VF.
 * @state: VF state bits
 */
struct intel_iov_data {
	unsigned long state;
#define IOV_VF_FLR_IN_PROGRESS		0
#define IOV_VF_NEEDS_FLR_START		1
#define IOV_VF_FLR_DONE_RECEIVED	2
#define IOV_VF_NEEDS_FLR_FINISH		3
#define IOV_VF_FLR_FAILED		(BITS_PER_LONG - 1)
	unsigned int adverse_events[IOV_THRESHOLD_MAX];
};

/**
 * struct intel_iov_state - Placeholder for all VFs data.
 * @worker: event processing worker
 */
struct intel_iov_state {
	struct work_struct worker;
	struct intel_iov_data *data;
};

/**
 * struct intel_iov_runtime_regs - Register runtime info shared with VFs.
 * @size: size of the regs and value arrays.
 * @regs: pointer to static array with register offsets.
 * @values: pointer to array with captured register values.
 */
struct intel_iov_runtime_regs {
	u32 size;
	const i915_reg_t *regs;
	u32 *values;
};

/**
 * struct intel_iov_service - Placeholder for service data shared with VFs.
 * @runtime: register runtime info shared with VFs.
 */
struct intel_iov_service {
	struct intel_iov_runtime_regs runtime;
};

/**
 * struct intel_iov_vf_runtime - Placeholder for the VF runtime data.
 * @regs_size: size of runtime register array.
 * @regs: pointer to array of register offset/value pairs.
 */
struct intel_iov_vf_runtime {
	u32 regs_size;
	struct vf_runtime_reg {
		u32 offset;
		u32 value;
	} *regs;
};

/**
 * struct intel_iov_relay - IOV Relay Communication data.
 * @lock: protects #pending_relays and #last_fence.
 * @pending_relays: list of relay requests that await a response.
 * @last_fence: fence used with last message.
 */
struct intel_iov_relay {
	spinlock_t lock;
	struct list_head pending_relays;
	u32 last_fence;

	I915_SELFTEST_DECLARE(struct {
		int (*host2guc)(struct intel_iov_relay *, const u32 *, u32);
		int (*guc2pf)(struct intel_iov_relay *, const u32 *, u32);
		int (*guc2vf)(struct intel_iov_relay *, const u32 *, u32);
		void *data;
		bool disable_strict : 1;
		bool enable_loopback : 1;
	} selftest);
};

/**
 * struct intel_iov_vf_config - VF configuration data.
 * @ggtt_base: base of GGTT region.
 * @ggtt_size: size of GGTT region.
 * @num_ctxs: number of GuC submission contexts.
 * @num_dbs: number of GuC doorbells.
 */
struct intel_iov_vf_config {
	u64 ggtt_base;
	u64 ggtt_size;
	u16 num_ctxs;
	u16 num_dbs;
};

/**
 * struct intel_iov - I/O Virtualization related data.
 * @pf.sysfs: sysfs data.
 * @pf.provisioning: provisioning data.
 * @pf.service: placeholder for service data.
 * @pf.state: placeholder for VFs data.
 * @vf.config: configuration of the resources assigned to VF.
 * @vf.runtime: retrieved runtime info.
 * @relay: data related to VF/PF communication based on GuC Relay messages.
 */
struct intel_iov {
	union {
		struct {
			struct intel_iov_sysfs sysfs;
			struct intel_iov_provisioning provisioning;
			struct intel_iov_service service;
			struct intel_iov_state state;
		} pf;

		struct {
			struct intel_iov_vf_config config;
			struct intel_iov_vf_runtime runtime;
			struct drm_mm_node ggtt_balloon[2];
		} vf;
	};

	struct intel_iov_relay relay;
};

#endif /* __INTEL_IOV_TYPES_H__ */
