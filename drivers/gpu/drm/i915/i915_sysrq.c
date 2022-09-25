// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/sysrq.h>

#include "gt/intel_engine.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_timeline.h"

#include "i915_drv.h"
#include "i915_request.h"
#include "i915_sysrq.h"
#include "i915_irq.h"
#include "intel_wakeref.h"

static DEFINE_MUTEX(sysrq_mutex);
static LIST_HEAD(sysrq_list);

struct sysrq_cb {
	struct list_head link;
	struct rcu_head rcu;

	void (*fn)(void *data);
	void *data;
};

static void sysrq_handle_showgpu(int key)
{
	struct sysrq_cb *cb;

	rcu_read_lock();
	list_for_each_entry(cb, &sysrq_list, link)
		cb->fn(cb->data);
	rcu_read_unlock();
}

static const struct sysrq_key_op sysrq_showgpu_op = {
		.handler        = sysrq_handle_showgpu,
		.help_msg       = "show-gpu(G)",
		.action_msg     = "Show GPU state",
		.enable_mask    = SYSRQ_ENABLE_DUMP,
};

static int register_sysrq(void (*fn)(void *data), void *data)
{
	struct sysrq_cb *cb;
	int ret = 0;

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;

	cb->fn = fn;
	cb->data = data;

	mutex_lock(&sysrq_mutex);
	if (list_empty(&sysrq_list))
		ret = register_sysrq_key('G', &sysrq_showgpu_op);
	if (ret == 0)
		list_add_tail_rcu(&cb->link, &sysrq_list);
	else
		kfree(cb);
	mutex_unlock(&sysrq_mutex);

	return ret;
}

static void unregister_sysrq(void (*fn)(void *data), void *data)
{
	struct sysrq_cb *cb;

	mutex_lock(&sysrq_mutex);
	list_for_each_entry(cb, &sysrq_list, link) {
		if (cb->fn == fn && cb->data == data) {
			list_del_rcu(&cb->link);
			if (list_empty(&sysrq_list))
				unregister_sysrq_key('G', &sysrq_showgpu_op);
			kfree_rcu(cb, rcu);
			break;
		}
	}
	mutex_unlock(&sysrq_mutex);

	/* Flush the handler before our caller can free fn/data */
	synchronize_rcu();
}

static void show_gpu_mem(struct drm_i915_private *i915, struct drm_printer *p)
{
	struct intel_memory_region *mr;
	enum intel_region_id id;

	for_each_memory_region(mr, i915, id)
		drm_printf(p, "%s: total:%pa, available:%pa bytes\n",
			   mr->name, &mr->total, &mr->avail);
}

static void show_gt(struct intel_gt *gt, struct drm_printer *p)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	drm_printf(p, "GT awake? %s [%d], %llums\n",
		   str_yes_no(gt->awake),
		   atomic_read(&gt->wakeref.count),
		   ktime_to_ms(intel_gt_get_awake_time(gt)));
	if (gt->awake)
		intel_wakeref_show(&gt->wakeref, p);

	for_each_engine(engine, gt, id) {
		if (intel_engine_is_idle(engine))
			continue;

		intel_engine_dump(engine, p, "%s\n", engine->name);
	}

	intel_gt_show_timelines(gt, p, i915_request_show_with_schedule);
}

static void show_rpm(struct drm_i915_private *i915, struct drm_printer *p)
{
	drm_printf(p, "Runtime power status: %s\n",
		   str_enabled_disabled(!i915->power_domains.init_wakeref));
	drm_printf(p, "IRQs disabled: %s\n",
		   str_yes_no(!intel_irqs_enabled(i915)));
	print_intel_runtime_pm_wakeref(&i915->runtime_pm, p);
}

static void show_gpu(void *data)
{
	struct drm_i915_private *i915 = data;
	struct drm_printer p = drm_info_printer(i915->drm.dev);

	show_rpm(i915, &p);
	show_gt(to_gt(i915), &p);
	show_gpu_mem(i915, &p);
}

int i915_register_sysrq(struct drm_i915_private *i915)
{
	return register_sysrq(show_gpu, i915);
}

void i915_unregister_sysrq(struct drm_i915_private *i915)
{
	unregister_sysrq(show_gpu, i915);
}
