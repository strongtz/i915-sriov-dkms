// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */
#include <linux/workqueue.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_gt_types.h"

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"

#include "intel_pxp.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_session.h"
#include "intel_pxp_types.h"
#include "intel_runtime_pm.h"

/**
 * intel_pxp_get_irq_gt - Find the correct GT that owns KCR interrupts
 * @pxp: pointer to pxp struct
 *
 * For platforms with a single GT, we return the pxp->ctrl_gt (as expected)
 * but for MTL+ that has a media-tile, although the KCR engine is in the
 * media-tile (i.e. pxp->ctrl_gt), the IRQ controls are on the root tile.
 * In the end, we don't use pxp->ctrl_gt for IRQ, we always return root gt.
 */
struct intel_gt *intel_pxp_get_irq_gt(struct intel_pxp *pxp)
{
	WARN_ON_ONCE(!pxp->ctrl_gt->i915->media_gt && !gt_is_root(pxp->ctrl_gt));

	return to_gt(pxp->ctrl_gt->i915);
}

/**
 * intel_pxp_irq_handler - Handles PXP interrupts.
 * @pxp: pointer to pxp struct
 * @iir: interrupt vector
 */
void intel_pxp_irq_handler(struct intel_pxp *pxp, u16 iir)
{
	struct intel_gt *gt;

	if (GEM_WARN_ON(!intel_pxp_is_enabled(pxp)))
		return;

	gt = intel_pxp_get_irq_gt(pxp);

	lockdep_assert_held(gt->irq_lock);

	if (unlikely(!iir))
		return;

	if (iir & (GEN12_DISPLAY_PXP_STATE_TERMINATED_INTERRUPT |
		   GEN12_DISPLAY_APP_TERMINATED_PER_FW_REQ_INTERRUPT)) {
		/* immediately mark PXP as inactive on termination */
		intel_pxp_mark_termination_in_progress(pxp);
		pxp->session_events |= PXP_TERMINATION_REQUEST | PXP_INVAL_REQUIRED;
	}

	if (iir & GEN12_DISPLAY_STATE_RESET_COMPLETE_INTERRUPT)
		pxp->session_events |= PXP_TERMINATION_COMPLETE;

	if (pxp->session_events)
		queue_work(system_unbound_wq, &pxp->session_work);
}

static inline void __pxp_set_interrupts(struct intel_gt *gt, u32 interrupts)
{
	struct intel_uncore *uncore = gt->uncore;
	const u32 mask = interrupts << 16;

	intel_uncore_write(uncore, GEN11_CRYPTO_RSVD_INTR_ENABLE, mask);
	intel_uncore_write(uncore, GEN11_CRYPTO_RSVD_INTR_MASK,  ~mask);
}

static inline void pxp_irq_reset(struct intel_gt *gt)
{
	spin_lock_irq(gt->irq_lock);
	gen11_gt_reset_one_iir(gt, 0, GEN11_KCR);
	spin_unlock_irq(gt->irq_lock);
}

void intel_pxp_irq_enable(struct intel_pxp *pxp)
{
	struct intel_gt *gt = intel_pxp_get_irq_gt(pxp);

	spin_lock_irq(gt->irq_lock);

	if (!pxp->irq_enabled)
		WARN_ON_ONCE(gen11_gt_reset_one_iir(gt, 0, GEN11_KCR));

	__pxp_set_interrupts(gt, GEN12_PXP_INTERRUPTS);
	pxp->irq_enabled = true;

	spin_unlock_irq(gt->irq_lock);
}

void intel_pxp_irq_disable(struct intel_pxp *pxp)
{
	struct intel_gt *gt = intel_pxp_get_irq_gt(pxp);

	/*
	 * We always need to submit a global termination when we re-enable the
	 * interrupts, so there is no need to make sure that the session state
	 * makes sense at the end of this function. Just make sure this is not
	 * called in a path were the driver consider the session as valid and
	 * doesn't call a termination on restart.
	 */
	GEM_WARN_ON(intel_pxp_is_active(pxp));

	spin_lock_irq(gt->irq_lock);

	pxp->irq_enabled = false;
	__pxp_set_interrupts(gt, 0);

	spin_unlock_irq(gt->irq_lock);
	intel_synchronize_irq(gt->i915);

	pxp_irq_reset(gt);

	flush_work(&pxp->session_work);
}
