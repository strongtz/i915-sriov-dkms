// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_memirq.h"
#include "intel_iov_reg.h"
#include "intel_iov_utils.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "gt/intel_breadcrumbs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_print.h"
#include "gt/intel_gt_regs.h"
#include "gt/uc/abi/guc_actions_vf_abi.h"

#ifdef CONFIG_DRM_I915_DEBUG_IOV
#define MEMIRQ_DEBUG(_gt, _f, ...) gt_dbg((_gt), "IRQ " _f, ##__VA_ARGS__)
#else
#define MEMIRQ_DEBUG(...)
#endif

/*
 * Memory based irq page layout
 * We use a single page to contain the different objects used for memory
 * based irq (which are also called "page" in the specs, even if they
 * aren't page-sized). The address of those objects are them programmed
 * in the HW via LRI and LRM in the context image.
 *
 * - Interrupt Status Report page: this page contains the interrupt
 *   status vectors for each unit. Each bit in the interrupt vectors is
 *   converted to a byte, with the byte being set to 0xFF when an
 *   interrupt is triggered; interrupt vectors are 16b big so each unit
 *   gets 16B. One space is reserved for each bit in one of the
 *   GEN11_GT_INTR_DWx registers, so this object needs a total of 1024B.
 *   This object needs to be 4k aligned.
 *
 * - Interrupt Source Report page: this is the equivalent of the
 *   GEN11_GT_INTR_DWx registers, with each bit in those registers being
 *   mapped to a byte here. The offsets are the same, just bytes instead
 *   of bits. This object needs to be cacheline aligned.
 *
 * - Interrupt Mask: the HW needs a location to fetch the interrupt
 *   mask vector to be used by the LRM in the context, so we just use
 *   the next available space in the interrupt page
 */

static int vf_create_memirq_data(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_i915(iov);
	struct drm_i915_gem_object *obj;
	void *vaddr;
	int err;
	u32 *enable_vector;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(!HAS_MEMORY_IRQ_STATUS(i915));
	GEM_BUG_ON(iov->vf.irq.obj);

	obj = i915_gem_object_create_shmem(i915, SZ_4K);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out;
	}

	vaddr = i915_gem_object_pin_map_unlocked(obj,
						 intel_gt_coherent_map_type(iov_to_gt(iov), obj,
									    true));
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_obj;
	}

	iov->vf.irq.obj = obj;
	iov->vf.irq.vaddr = vaddr;

	enable_vector = (u32 *)(vaddr + I915_VF_IRQ_ENABLE);
	/*XXX: we should start with all irqs disabled: 0xffff0000 */
	*enable_vector = 0xffff;

	return 0;

out_obj:
	i915_gem_object_put(obj);
out:
	IOV_DEBUG(iov, "failed %d\n", err);
	return err;
}

static int vf_map_memirq_data(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);
	struct i915_vma *vma;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(!iov->vf.irq.obj);

	vma = i915_vma_instance(iov->vf.irq.obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto out_vma;

	iov->vf.irq.vma = vma;

	return 0;

out_vma:
	i915_gem_object_put(iov->vf.irq.obj);
out:
	IOV_DEBUG(iov, "failed %pe\n", ERR_PTR(err));
	return err;
}

static void vf_release_memirq_data(struct intel_iov *iov)
{
	i915_vma_unpin_and_release(&iov->vf.irq.vma, I915_VMA_RELEASE_MAP);
	iov->vf.irq.obj = NULL;
	iov->vf.irq.vaddr = NULL;
}

/**
 * intel_iov_memirq_init - Initialize data used by memory based interrupts.
 * @iov: the IOV struct
 *
 * Allocate Interrupt Source Report page and Interrupt Status Report page
 * used by memory based interrupts.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_memirq_init(struct intel_iov *iov)
{
	int err;

	if (!HAS_MEMORY_IRQ_STATUS(iov_to_i915(iov)))
		return 0;

	err = vf_create_memirq_data(iov);
	if (unlikely(err))
		return err;

	err = vf_map_memirq_data(iov);
	if (unlikely(err))
		return err;

	return 0;
}

/**
 * intel_iov_memirq_fini - Release data used by memory based interrupts.
 * @iov: the IOV struct
 *
 * Release data used by memory based interrupts.
 */
void intel_iov_memirq_fini(struct intel_iov *iov)
{
	if (!HAS_MEMORY_IRQ_STATUS(iov_to_i915(iov)))
		return;

	vf_release_memirq_data(iov);
}

/**
 * intel_iov_memirq_prepare_guc - Prepare GuC to use memory based interrrupts.
 * @iov: the IOV struct
 *
 * Register Interrupt Source Report page and Interrupt Status Report page
 * within GuC to correctly handle memory based interrrupts from GuC.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_memirq_prepare_guc(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);
	struct intel_guc *guc = &gt->uc.guc;
	u32 base, source, status;
	int err;

	GEM_BUG_ON(!intel_iov_is_vf(iov));
	GEM_BUG_ON(!HAS_MEMORY_IRQ_STATUS(iov_to_i915(iov)));

	base = intel_guc_ggtt_offset(guc, iov->vf.irq.vma);
	source = base + I915_VF_IRQ_SOURCE + GEN11_GUC;
	status = base + I915_VF_IRQ_STATUS + GEN11_GUC * SZ_16;

	err = intel_guc_self_cfg64(guc, GUC_KLV_SELF_CFG_MEMIRQ_SOURCE_ADDR_KEY,
				   source);
	if (unlikely(err))
		goto failed;

	err = intel_guc_self_cfg64(guc, GUC_KLV_SELF_CFG_MEMIRQ_STATUS_ADDR_KEY,
				   status);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	IOV_ERROR(iov, "Failed to register MEMIRQ %#x:%#x (%pe)\n",
		  source, status, ERR_PTR(err));
	return err;
}

/**
 * intel_iov_memirq_reset - TBD
 * @iov: the IOV struct
 *
 * TBD.
 */
void intel_iov_memirq_reset(struct intel_iov *iov)
{
	u8 *irq = iov->vf.irq.vaddr;
	u32 *val = (u32 *)(irq + I915_VF_IRQ_ENABLE);

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	if (irq)
		*val = 0;
}

/**
 * intel_iov_memirq_postinstall - TBD
 * @iov: the IOV struct
 *
 * TBD.
 */
void intel_iov_memirq_postinstall(struct intel_iov *iov)
{
	u8 *irq = iov->vf.irq.vaddr;
	u32 *val = (u32 *)(irq + I915_VF_IRQ_ENABLE);

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	if (irq)
		*val = 0xffff;
}

static void __engine_mem_irq_handler(struct intel_engine_cs *engine, u8 *status)
{
	struct intel_gt __maybe_unused *gt = engine->gt;

	MEMIRQ_DEBUG(gt, "STATUS %s %*ph\n", engine->name, 16, status);

	if (READ_ONCE(status[ilog2(GT_RENDER_USER_INTERRUPT)]) == 0xFF) {
		WRITE_ONCE(status[ilog2(GT_RENDER_USER_INTERRUPT)], 0x00);
		intel_engine_signal_breadcrumbs(engine);
		tasklet_hi_schedule(&engine->sched_engine->tasklet);
	}
}

static void __guc_mem_irq_handler(struct intel_guc *guc, u8 *status)
{
	struct intel_gt __maybe_unused *gt = guc_to_gt(guc);

	MEMIRQ_DEBUG(gt, "STATUS %s %*ph\n", "GUC", 16, status);

	if (READ_ONCE(status[ilog2(GUC_INTR_GUC2HOST)]) == 0xFF) {
		WRITE_ONCE(status[ilog2(GUC_INTR_GUC2HOST)], 0x00);
		intel_guc_to_host_event_handler(guc);
	}
}

/**
 * intel_iov_memirq_handler - TBD
 * @iov: the IOV struct
 *
 * TBD.
 */
void intel_iov_memirq_handler(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);
	u8 *irq = iov->vf.irq.vaddr;
	u8 * const source_base = irq + I915_VF_IRQ_SOURCE;
	u8 * const status_base = irq + I915_VF_IRQ_STATUS;
	u8 *source, value;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	GEM_BUG_ON(!intel_iov_is_vf(iov));

	if (!irq)
		return;

	MEMIRQ_DEBUG(gt, "SOURCE %*ph\n", 32, source_base);
	MEMIRQ_DEBUG(gt, "SOURCE %*ph\n", 32, source_base + 32);

	/* TODO: Only check active engines */
	for_each_engine(engine, gt, id) {
		source = source_base + engine->irq_offset;
		value = READ_ONCE(*source);
		if (value == 0xff) {
			WRITE_ONCE(*source, 0x00);
			__engine_mem_irq_handler(engine, status_base +
						 engine->irq_offset * SZ_16);
		}
	}

	/* GuC must be check separately */
	source = source_base + GEN11_GUC;
	value = READ_ONCE(*source);
	if (value == 0xff) {
		WRITE_ONCE(*source, 0x00);
		__guc_mem_irq_handler(&gt->uc.guc, status_base +
				      GEN11_GUC * SZ_16);
	}
}
