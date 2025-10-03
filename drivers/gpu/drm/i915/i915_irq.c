/* i915_irq.c -- IRQ support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/sysrq.h>

#include <drm/drm_drv.h>

#include "display/intel_display_core.h"
#include "display/intel_display_irq.h"
#include "display/intel_hotplug.h"
#include "display/intel_hotplug_irq.h"
#include "display/intel_lpe_audio.h"
#include "display/intel_psr_regs.h"

#include "gt/intel_breadcrumbs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm_irq.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_rps.h"
#include "gt/iov/intel_iov_memirq.h"

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"

/**
 * DOC: interrupt handling
 *
 * These functions provide the basic support for enabling and disabling the
 * interrupt handling support. There's a lot more functionality in i915_irq.c
 * and related files, but that will be described in separate chapters.
 */

/*
 * Interrupt statistic for PMU. Increments the counter only if the
 * interrupt originated from the GPU so interrupts from a device which
 * shares the interrupt line are not accounted.
 */
static inline void pmu_irq_stats(struct drm_i915_private *i915,
				 irqreturn_t res)
{
	if (unlikely(res != IRQ_HANDLED))
		return;

	/*
	 * A clever compiler translates that into INC. A not so clever one
	 * should at least prevent store tearing.
	 */
	WRITE_ONCE(i915->pmu.irq_count, i915->pmu.irq_count + 1);
}

void gen2_irq_reset(struct intel_uncore *uncore, struct i915_irq_regs regs)
{
	intel_uncore_write(uncore, regs.imr, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.imr);

	intel_uncore_write(uncore, regs.ier, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write(uncore, regs.iir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.iir);
	intel_uncore_write(uncore, regs.iir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.iir);
}

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
void gen2_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg)
{
	u32 val = intel_uncore_read(uncore, reg);

	if (val == 0)
		return;

	drm_WARN(&uncore->i915->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 i915_mmio_reg_offset(reg), val);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
}

void gen2_irq_init(struct intel_uncore *uncore, struct i915_irq_regs regs,
		   u32 imr_val, u32 ier_val)
{
	gen2_assert_iir_is_zero(uncore, regs.iir);

	intel_uncore_write(uncore, regs.ier, ier_val);
	intel_uncore_write(uncore, regs.imr, imr_val);
	intel_uncore_posting_read(uncore, regs.imr);
}

void gen2_error_reset(struct intel_uncore *uncore, struct i915_error_regs regs)
{
	intel_uncore_write(uncore, regs.emr, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.emr);

	intel_uncore_write(uncore, regs.eir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.eir);
	intel_uncore_write(uncore, regs.eir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.eir);
}

void gen2_error_init(struct intel_uncore *uncore, struct i915_error_regs regs,
		     u32 emr_val)
{
	intel_uncore_write(uncore, regs.eir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.eir);
	intel_uncore_write(uncore, regs.eir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.eir);

	intel_uncore_write(uncore, regs.emr, emr_val);
	intel_uncore_posting_read(uncore, regs.emr);
}

/**
 * ivb_parity_work - Workqueue called when a parity error interrupt
 * occurred.
 * @work: workqueue struct
 *
 * Doesn't actually do anything except notify userspace. As a consequence of
 * this event, userspace should try to remap the bad rows since statistically
 * it is likely the same row is more likely to go bad again.
 */
static void ivb_parity_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), l3_parity.error_work);
	struct intel_gt *gt = to_gt(dev_priv);
	u32 error_status, row, bank, subbank;
	char *parity_event[6];
	u32 misccpctl;
	u8 slice = 0;

	/* We must turn off DOP level clock gating to access the L3 registers.
	 * In order to prevent a get/put style interface, acquire struct mutex
	 * any time we access those registers.
	 */
	mutex_lock(&dev_priv->drm.struct_mutex);

	/* If we've screwed up tracking, just let the interrupt fire again */
	if (drm_WARN_ON(&dev_priv->drm, !dev_priv->l3_parity.which_slice))
		goto out;

	misccpctl = intel_uncore_rmw(&dev_priv->uncore, GEN7_MISCCPCTL,
				     GEN7_DOP_CLOCK_GATE_ENABLE, 0);
	intel_uncore_posting_read(&dev_priv->uncore, GEN7_MISCCPCTL);

	while ((slice = ffs(dev_priv->l3_parity.which_slice)) != 0) {
		i915_reg_t reg;

		slice--;
		if (drm_WARN_ON_ONCE(&dev_priv->drm,
				     slice >= NUM_L3_SLICES(dev_priv)))
			break;

		dev_priv->l3_parity.which_slice &= ~(1<<slice);

		reg = GEN7_L3CDERRST1(slice);

		error_status = intel_uncore_read(&dev_priv->uncore, reg);
		row = GEN7_PARITY_ERROR_ROW(error_status);
		bank = GEN7_PARITY_ERROR_BANK(error_status);
		subbank = GEN7_PARITY_ERROR_SUBBANK(error_status);

		intel_uncore_write(&dev_priv->uncore, reg, GEN7_PARITY_ERROR_VALID | GEN7_L3CDERRST1_ENABLE);
		intel_uncore_posting_read(&dev_priv->uncore, reg);

		parity_event[0] = I915_L3_PARITY_UEVENT "=1";
		parity_event[1] = kasprintf(GFP_KERNEL, "ROW=%d", row);
		parity_event[2] = kasprintf(GFP_KERNEL, "BANK=%d", bank);
		parity_event[3] = kasprintf(GFP_KERNEL, "SUBBANK=%d", subbank);
		parity_event[4] = kasprintf(GFP_KERNEL, "SLICE=%d", slice);
		parity_event[5] = NULL;

		kobject_uevent_env(&dev_priv->drm.primary->kdev->kobj,
				   KOBJ_CHANGE, parity_event);

		drm_dbg(&dev_priv->drm,
			"Parity error: Slice = %d, Row = %d, Bank = %d, Sub bank = %d.\n",
			slice, row, bank, subbank);

		kfree(parity_event[4]);
		kfree(parity_event[3]);
		kfree(parity_event[2]);
		kfree(parity_event[1]);
	}

	intel_uncore_write(&dev_priv->uncore, GEN7_MISCCPCTL, misccpctl);

out:
	drm_WARN_ON(&dev_priv->drm, dev_priv->l3_parity.which_slice);
	spin_lock_irq(gt->irq_lock);
	gen5_gt_enable_irq(gt, GT_PARITY_ERROR(dev_priv));
	spin_unlock_irq(gt->irq_lock);

	mutex_unlock(&dev_priv->drm.struct_mutex);
}

static irqreturn_t valleyview_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct intel_display *display = dev_priv->display;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 iir, gt_iir, pm_iir;
		u32 eir = 0, dpinvgtt = 0;
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
		u32 ier = 0;

		gt_iir = intel_uncore_read(&dev_priv->uncore, GTIIR);
		pm_iir = intel_uncore_read(&dev_priv->uncore, GEN6_PMIIR);
		iir = intel_uncore_read(&dev_priv->uncore, VLV_IIR);

		if (gt_iir == 0 && pm_iir == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		/*
		 * Theory on interrupt generation, based on empirical evidence:
		 *
		 * x = ((VLV_IIR & VLV_IER) ||
		 *      (((GT_IIR & GT_IER) || (GEN6_PMIIR & GEN6_PMIER)) &&
		 *       (VLV_MASTER_IER & MASTER_INTERRUPT_ENABLE)));
		 *
		 * A CPU interrupt will only be raised when 'x' has a 0->1 edge.
		 * Hence we clear MASTER_INTERRUPT_ENABLE and VLV_IER to
		 * guarantee the CPU interrupt will be raised again even if we
		 * don't end up clearing all the VLV_IIR, GT_IIR, GEN6_PMIIR
		 * bits this time around.
		 */
		intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, 0);
		ier = intel_uncore_rmw(&dev_priv->uncore, VLV_IER, ~0, 0);

		if (gt_iir)
			intel_uncore_write(&dev_priv->uncore, GTIIR, gt_iir);
		if (pm_iir)
			intel_uncore_write(&dev_priv->uncore, GEN6_PMIIR, pm_iir);

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(display);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			vlv_display_error_irq_ack(display, &eir, &dpinvgtt);

		/* Call regardless, as some status bits might not be
		 * signalled in IIR */
		i9xx_pipestat_irq_ack(display, iir, pipe_stats);

		if (iir & (I915_LPE_PIPE_A_INTERRUPT |
			   I915_LPE_PIPE_B_INTERRUPT))
			intel_lpe_audio_irq_handler(display);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			intel_uncore_write(&dev_priv->uncore, VLV_IIR, iir);

		intel_uncore_write(&dev_priv->uncore, VLV_IER, ier);
		intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);

		if (gt_iir)
			gen6_gt_irq_handler(to_gt(dev_priv), gt_iir);
		if (pm_iir)
			gen6_rps_irq_handler(&to_gt(dev_priv)->rps, pm_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(display, hotplug_status);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			vlv_display_error_irq_handler(display, eir, dpinvgtt);

		valleyview_pipestat_irq_handler(display, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static irqreturn_t cherryview_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct intel_display *display = dev_priv->display;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 master_ctl, iir;
		u32 eir = 0, dpinvgtt = 0;
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
		u32 ier = 0;

		master_ctl = intel_uncore_read(&dev_priv->uncore, GEN8_MASTER_IRQ) & ~GEN8_MASTER_IRQ_CONTROL;
		iir = intel_uncore_read(&dev_priv->uncore, VLV_IIR);

		if (master_ctl == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		/*
		 * Theory on interrupt generation, based on empirical evidence:
		 *
		 * x = ((VLV_IIR & VLV_IER) ||
		 *      ((GEN8_MASTER_IRQ & ~GEN8_MASTER_IRQ_CONTROL) &&
		 *       (GEN8_MASTER_IRQ & GEN8_MASTER_IRQ_CONTROL)));
		 *
		 * A CPU interrupt will only be raised when 'x' has a 0->1 edge.
		 * Hence we clear GEN8_MASTER_IRQ_CONTROL and VLV_IER to
		 * guarantee the CPU interrupt will be raised again even if we
		 * don't end up clearing all the VLV_IIR and GEN8_MASTER_IRQ_CONTROL
		 * bits this time around.
		 */
		intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, 0);
		ier = intel_uncore_rmw(&dev_priv->uncore, VLV_IER, ~0, 0);

		gen8_gt_irq_handler(to_gt(dev_priv), master_ctl);

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(display);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			vlv_display_error_irq_ack(display, &eir, &dpinvgtt);

		/* Call regardless, as some status bits might not be
		 * signalled in IIR */
		i9xx_pipestat_irq_ack(display, iir, pipe_stats);

		if (iir & (I915_LPE_PIPE_A_INTERRUPT |
			   I915_LPE_PIPE_B_INTERRUPT |
			   I915_LPE_PIPE_C_INTERRUPT))
			intel_lpe_audio_irq_handler(display);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			intel_uncore_write(&dev_priv->uncore, VLV_IIR, iir);

		intel_uncore_write(&dev_priv->uncore, VLV_IER, ier);
		intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);

		if (hotplug_status)
			i9xx_hpd_irq_handler(display, hotplug_status);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			vlv_display_error_irq_handler(display, eir, dpinvgtt);

		valleyview_pipestat_irq_handler(display, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

/*
 * To handle irqs with the minimum potential races with fresh interrupts, we:
 * 1 - Disable Master Interrupt Control.
 * 2 - Find the source(s) of the interrupt.
 * 3 - Clear the Interrupt Identity bits (IIR).
 * 4 - Process the interrupt(s) that had bits set in the IIRs.
 * 5 - Re-enable Master Interrupt Control.
 */
static irqreturn_t ilk_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_display *display = i915->display;
	void __iomem * const regs = intel_uncore_regs(&i915->uncore);
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	if (unlikely(!intel_irqs_enabled(i915)))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	/* disable master interrupt before clearing iir  */
	de_ier = raw_reg_read(regs, DEIER);
	raw_reg_write(regs, DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	if (!HAS_PCH_NOP(i915)) {
		sde_ier = raw_reg_read(regs, SDEIER);
		raw_reg_write(regs, SDEIER, 0);
	}

	/* Find, clear, then process each source of interrupt */

	gt_iir = raw_reg_read(regs, GTIIR);
	if (gt_iir) {
		raw_reg_write(regs, GTIIR, gt_iir);
		if (GRAPHICS_VER(i915) >= 6)
			gen6_gt_irq_handler(to_gt(i915), gt_iir);
		else
			gen5_gt_irq_handler(to_gt(i915), gt_iir);
		ret = IRQ_HANDLED;
	}

	de_iir = raw_reg_read(regs, DEIIR);
	if (de_iir) {
		raw_reg_write(regs, DEIIR, de_iir);
		if (DISPLAY_VER(i915) >= 7)
			ivb_display_irq_handler(display, de_iir);
		else
			ilk_display_irq_handler(display, de_iir);
		ret = IRQ_HANDLED;
	}

	if (GRAPHICS_VER(i915) >= 6) {
		u32 pm_iir = raw_reg_read(regs, GEN6_PMIIR);
		if (pm_iir) {
			raw_reg_write(regs, GEN6_PMIIR, pm_iir);
			gen6_rps_irq_handler(&to_gt(i915)->rps, pm_iir);
			ret = IRQ_HANDLED;
		}
	}

	raw_reg_write(regs, DEIER, de_ier);
	if (sde_ier)
		raw_reg_write(regs, SDEIER, sde_ier);

	pmu_irq_stats(i915, ret);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	return ret;
}

static inline u32 gen8_master_intr_disable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN8_MASTER_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return raw_reg_read(regs, GEN8_MASTER_IRQ);
}

static inline void gen8_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
}

static irqreturn_t gen8_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct intel_display *display = dev_priv->display;
	void __iomem * const regs = intel_uncore_regs(&dev_priv->uncore);
	u32 master_ctl;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	master_ctl = gen8_master_intr_disable(regs);
	if (!master_ctl) {
		gen8_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, queue (onto bottom-halves), then clear each source */
	gen8_gt_irq_handler(to_gt(dev_priv), master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & ~GEN8_GT_IRQS) {
		disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
		gen8_de_irq_handler(display, master_ctl);
		enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
	}

	gen8_master_intr_enable(regs);

	pmu_irq_stats(dev_priv, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static inline u32 gen11_master_intr_disable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return raw_reg_read(regs, GEN11_GFX_MSTR_IRQ);
}

static inline void gen11_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);
}

static irqreturn_t gen11_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_display *display = i915->display;
	void __iomem * const regs = intel_uncore_regs(&i915->uncore);
	struct intel_gt *gt = to_gt(i915);
	u32 master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_ctl = gen11_master_intr_disable(regs);
	if (!master_ctl) {
		gen11_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, queue (onto bottom-halves), then clear each source */
	gen11_gt_irq_handler(gt, master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & GEN11_DISPLAY_IRQ)
		gen11_display_irq_handler(display);

	gu_misc_iir = gen11_gu_misc_irq_ack(display, master_ctl);

	gen11_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(display, gu_misc_iir);

	pmu_irq_stats(i915, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static inline u32 dg1_master_intr_disable(void __iomem * const regs)
{
	u32 val;

	/* First disable interrupts */
	raw_reg_write(regs, DG1_MSTR_TILE_INTR, 0);

	/* Get the indication levels and ack the master unit */
	val = raw_reg_read(regs, DG1_MSTR_TILE_INTR);
	if (unlikely(!val))
		return 0;

	raw_reg_write(regs, DG1_MSTR_TILE_INTR, val);

	return val;
}

static inline void dg1_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, DG1_MSTR_TILE_INTR, DG1_MSTR_IRQ);
}

static irqreturn_t dg1_irq_handler(int irq, void *arg)
{
	struct drm_i915_private * const i915 = arg;
	struct intel_display *display = i915->display;
	struct intel_gt *gt = to_gt(i915);
	void __iomem * const regs = intel_uncore_regs(gt->uncore);
	u32 master_tile_ctl, master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_tile_ctl = dg1_master_intr_disable(regs);
	if (!master_tile_ctl) {
		dg1_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* FIXME: we only support tile 0 for now. */
	if (master_tile_ctl & DG1_MSTR_TILE(0)) {
		master_ctl = raw_reg_read(regs, GEN11_GFX_MSTR_IRQ);
		raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, master_ctl);
	} else {
		drm_err(&i915->drm, "Tile not supported: 0x%08x\n",
			master_tile_ctl);
		dg1_master_intr_enable(regs);
		return IRQ_NONE;
	}

	gen11_gt_irq_handler(gt, master_ctl);

	if (master_ctl & GEN11_DISPLAY_IRQ)
		gen11_display_irq_handler(display);

	gu_misc_iir = gen11_gu_misc_irq_ack(display, master_ctl);

	dg1_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(display, gu_misc_iir);

	pmu_irq_stats(i915, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static irqreturn_t vf_mem_irq_handler(int irq, void *arg)
{
	struct drm_i915_private * const i915 = arg;
	struct intel_gt *gt;
	unsigned int i;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	for_each_gt(gt, i915, i)
		intel_iov_memirq_handler(&gt->iov);

	pmu_irq_stats(i915, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static void vf_mem_irq_reset(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int i;

	for_each_gt(gt, i915, i)
		intel_iov_memirq_reset(&gt->iov);
}

static int vf_mem_irq_postinstall(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int i;

	for_each_gt(gt, i915, i)
		intel_iov_memirq_postinstall(&gt->iov);

	return 0;
}

/* drm_dma.h hooks
*/
static void ilk_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;

	gen2_irq_reset(uncore, DE_IRQ_REGS);
	dev_priv->irq_mask = ~0u;

	if (GRAPHICS_VER(dev_priv) == 7)
		intel_uncore_write(uncore, GEN7_ERR_INT, 0xffffffff);

	if (IS_HASWELL(dev_priv)) {
		intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
		intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);
	}

	gen5_gt_irq_reset(to_gt(dev_priv));

	ibx_display_irq_reset(display);
}

static void valleyview_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;

	intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, 0);
	intel_uncore_posting_read(&dev_priv->uncore, VLV_MASTER_IER);

	gen5_gt_irq_reset(to_gt(dev_priv));

	vlv_display_irq_reset(display);
}

static void gen8_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;

	gen8_master_intr_disable(intel_uncore_regs(uncore));

	gen8_gt_irq_reset(to_gt(dev_priv));
	gen8_display_irq_reset(display);
	gen2_irq_reset(uncore, GEN8_PCU_IRQ_REGS);
}

static void gen11_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;

	gen11_master_intr_disable(intel_uncore_regs(&dev_priv->uncore));

	gen11_gt_irq_reset(gt);
	gen11_display_irq_reset(display);

	if (!IS_SRIOV_VF(dev_priv)) {
		gen2_irq_reset(uncore, GEN11_GU_MISC_IRQ_REGS);
		gen2_irq_reset(uncore, GEN8_PCU_IRQ_REGS);
	}
}

static void dg1_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;
	struct intel_gt *gt;
	unsigned int i;

	dg1_master_intr_disable(intel_uncore_regs(&dev_priv->uncore));

	for_each_gt(gt, dev_priv, i)
		gen11_gt_irq_reset(gt);

	gen11_display_irq_reset(display);

	gen2_irq_reset(uncore, GEN11_GU_MISC_IRQ_REGS);
	gen2_irq_reset(uncore, GEN8_PCU_IRQ_REGS);

	intel_uncore_write(uncore, GEN11_GFX_MSTR_IRQ, ~0);
}

static void cherryview_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;

	intel_uncore_write(uncore, GEN8_MASTER_IRQ, 0);
	intel_uncore_posting_read(&dev_priv->uncore, GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(to_gt(dev_priv));

	gen2_irq_reset(uncore, GEN8_PCU_IRQ_REGS);

	vlv_display_irq_reset(display);
}

static void ilk_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;

	gen5_gt_irq_postinstall(to_gt(dev_priv));

	ilk_de_irq_postinstall(display);
}

static void valleyview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;

	gen5_gt_irq_postinstall(to_gt(dev_priv));

	vlv_display_irq_postinstall(display);

	intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);
	intel_uncore_posting_read(&dev_priv->uncore, VLV_MASTER_IER);
}

static void gen8_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;

	gen8_gt_irq_postinstall(to_gt(dev_priv));
	gen8_de_irq_postinstall(display);

	gen8_master_intr_enable(intel_uncore_regs(&dev_priv->uncore));
}

static void gen11_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;

	gen11_gt_irq_postinstall(gt);
	gen11_de_irq_postinstall(display);

	if (!IS_SRIOV_VF(dev_priv))
		gen2_irq_init(uncore, GEN11_GU_MISC_IRQ_REGS, ~gu_misc_masked, gu_misc_masked);

	gen11_master_intr_enable(intel_uncore_regs(uncore));
	intel_uncore_posting_read(&dev_priv->uncore, GEN11_GFX_MSTR_IRQ);
}

static void dg1_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;
	struct intel_gt *gt;
	unsigned int i;

	for_each_gt(gt, dev_priv, i)
		gen11_gt_irq_postinstall(gt);

	gen2_irq_init(uncore, GEN11_GU_MISC_IRQ_REGS, ~gu_misc_masked, gu_misc_masked);

	dg1_de_irq_postinstall(display);

	dg1_master_intr_enable(intel_uncore_regs(uncore));
	intel_uncore_posting_read(uncore, DG1_MSTR_TILE_INTR);
}

static void cherryview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;

	gen8_gt_irq_postinstall(to_gt(dev_priv));

	vlv_display_irq_postinstall(display);

	intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	intel_uncore_posting_read(&dev_priv->uncore, GEN8_MASTER_IRQ);
}

static u32 i9xx_error_mask(struct drm_i915_private *i915)
{
	/*
	 * On gen2/3 FBC generates (seemingly spurious)
	 * display INVALID_GTT/INVALID_GTT_PTE table errors.
	 *
	 * Also gen3 bspec has this to say:
	 * "DISPA_INVALID_GTT_PTE
	 "  [DevNapa] : Reserved. This bit does not reflect the page
	 "              table error for the display plane A."
	 *
	 * Unfortunately we can't mask off individual PGTBL_ER bits,
	 * so we just have to mask off all page table errors via EMR.
	 */
	if (HAS_FBC(i915))
		return I915_ERROR_MEMORY_REFRESH;
	else
		return I915_ERROR_PAGE_TABLE |
			I915_ERROR_MEMORY_REFRESH;
}

static void i9xx_error_irq_ack(struct drm_i915_private *dev_priv,
			       u32 *eir, u32 *eir_stuck)
{
	u32 emr;

	*eir = intel_uncore_read(&dev_priv->uncore, EIR);
	intel_uncore_write(&dev_priv->uncore, EIR, *eir);

	*eir_stuck = intel_uncore_read(&dev_priv->uncore, EIR);
	if (*eir_stuck == 0)
		return;

	/*
	 * Toggle all EMR bits to make sure we get an edge
	 * in the ISR master error bit if we don't clear
	 * all the EIR bits. Otherwise the edge triggered
	 * IIR on i965/g4x wouldn't notice that an interrupt
	 * is still pending. Also some EIR bits can't be
	 * cleared except by handling the underlying error
	 * (or by a GPU reset) so we mask any bit that
	 * remains set.
	 */
	emr = intel_uncore_read(&dev_priv->uncore, EMR);
	intel_uncore_write(&dev_priv->uncore, EMR, 0xffffffff);
	intel_uncore_write(&dev_priv->uncore, EMR, emr | *eir_stuck);
}

static void i9xx_error_irq_handler(struct drm_i915_private *dev_priv,
				   u32 eir, u32 eir_stuck)
{
	drm_dbg(&dev_priv->drm, "Master Error, EIR 0x%08x\n", eir);

	if (eir_stuck)
		drm_dbg(&dev_priv->drm, "EIR stuck: 0x%08x, masked\n",
			eir_stuck);

	drm_dbg(&dev_priv->drm, "PGTBL_ER: 0x%08x\n",
		intel_uncore_read(&dev_priv->uncore, PGTBL_ER));
}

static void i915_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;

	i9xx_display_irq_reset(display);

	gen2_error_reset(uncore, GEN2_ERROR_REGS);
	gen2_irq_reset(uncore, GEN2_IRQ_REGS);
	dev_priv->irq_mask = ~0u;
}

static void i915_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;

	gen2_error_init(uncore, GEN2_ERROR_REGS, ~i9xx_error_mask(dev_priv));

	dev_priv->irq_mask =
		~(I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (DISPLAY_VER(dev_priv) >= 3) {
		dev_priv->irq_mask &= ~I915_ASLE_INTERRUPT;
		enable_mask |= I915_ASLE_INTERRUPT;
	}

	if (HAS_HOTPLUG(dev_priv)) {
		dev_priv->irq_mask &= ~I915_DISPLAY_PORT_INTERRUPT;
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
	}

	gen2_irq_init(uncore, GEN2_IRQ_REGS, dev_priv->irq_mask, enable_mask);

	i915_display_irq_postinstall(display);
}

static irqreturn_t i915_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct intel_display *display = dev_priv->display;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 eir = 0, eir_stuck = 0;
		u32 hotplug_status = 0;
		u32 iir;

		iir = intel_uncore_read(&dev_priv->uncore, GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		if (HAS_HOTPLUG(dev_priv) &&
		    iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(display);

		/* Call regardless, as some status bits might not be
		 * signalled in IIR */
		i9xx_pipestat_irq_ack(display, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		intel_uncore_write(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0], iir);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(display, hotplug_status);

		i915_pipestat_irq_handler(display, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i965_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;

	i9xx_display_irq_reset(display);

	gen2_error_reset(uncore, GEN2_ERROR_REGS);
	gen2_irq_reset(uncore, GEN2_IRQ_REGS);
	dev_priv->irq_mask = ~0u;
}

static u32 i965_error_mask(struct drm_i915_private *i915)
{
	/*
	 * Enable some error detection, note the instruction error mask
	 * bit is reserved, so we leave it masked.
	 *
	 * i965 FBC no longer generates spurious GTT errors,
	 * so we can always enable the page table errors.
	 */
	if (IS_G4X(i915))
		return GM45_ERROR_PAGE_TABLE |
			GM45_ERROR_MEM_PRIV |
			GM45_ERROR_CP_PRIV |
			I915_ERROR_MEMORY_REFRESH;
	else
		return I915_ERROR_PAGE_TABLE |
			I915_ERROR_MEMORY_REFRESH;
}

static void i965_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;

	gen2_error_init(uncore, GEN2_ERROR_REGS, ~i965_error_mask(dev_priv));

	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PORT_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (IS_G4X(dev_priv))
		enable_mask |= I915_BSD_USER_INTERRUPT;

	gen2_irq_init(uncore, GEN2_IRQ_REGS, dev_priv->irq_mask, enable_mask);

	i965_display_irq_postinstall(display);
}

static irqreturn_t i965_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct intel_display *display = dev_priv->display;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 eir = 0, eir_stuck = 0;
		u32 hotplug_status = 0;
		u32 iir;

		iir = intel_uncore_read(&dev_priv->uncore, GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(display);

		/* Call regardless, as some status bits might not be
		 * signalled in IIR */
		i9xx_pipestat_irq_ack(display, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		intel_uncore_write(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0],
					    iir);

		if (iir & I915_BSD_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[VCS0],
					    iir >> 25);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(display, hotplug_status);

		i965_pipestat_irq_handler(display, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, IRQ_HANDLED);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

/**
 * intel_irq_init - initializes irq support
 * @dev_priv: i915 device instance
 *
 * This function initializes all the irq support including work items, timers
 * and all the vtables. It does not setup the interrupt itself though.
 */
void intel_irq_init(struct drm_i915_private *dev_priv)
{
	int i;

	INIT_WORK(&dev_priv->l3_parity.error_work, ivb_parity_work);
	for (i = 0; i < MAX_L3_SLICES; ++i)
		dev_priv->l3_parity.remap_info[i] = NULL;

	/* pre-gen11 the guc irqs bits are in the upper 16 bits of the pm reg */
	if (HAS_GT_UC(dev_priv) && GRAPHICS_VER(dev_priv) < 11)
		to_gt(dev_priv)->pm_guc_events = GUC_INTR_GUC2HOST << 16;
}

/**
 * intel_irq_fini - deinitializes IRQ support
 * @i915: i915 device instance
 *
 * This function deinitializes all the IRQ support.
 */
void intel_irq_fini(struct drm_i915_private *i915)
{
	int i;

	for (i = 0; i < MAX_L3_SLICES; ++i)
		kfree(i915->l3_parity.remap_info[i]);
}

static irq_handler_t intel_irq_handler(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			return cherryview_irq_handler;
		else if (IS_VALLEYVIEW(dev_priv))
			return valleyview_irq_handler;
		else if (GRAPHICS_VER(dev_priv) == 4)
			return i965_irq_handler;
		else
			return i915_irq_handler;
	} else {
		if (HAS_MEMORY_IRQ_STATUS(dev_priv))
			return vf_mem_irq_handler;
		else if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			return dg1_irq_handler;
		else if (GRAPHICS_VER(dev_priv) >= 11)
			return gen11_irq_handler;
		else if (GRAPHICS_VER(dev_priv) >= 8)
			return gen8_irq_handler;
		else
			return ilk_irq_handler;
	}
}

static void intel_irq_reset(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_reset(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 4)
			i965_irq_reset(dev_priv);
		else
			i915_irq_reset(dev_priv);
	} else {
		if (HAS_MEMORY_IRQ_STATUS(dev_priv))
			vf_mem_irq_reset(dev_priv);
		else if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			dg1_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 11)
			gen11_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 8)
			gen8_irq_reset(dev_priv);
		else
			ilk_irq_reset(dev_priv);
	}
}

static void intel_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_postinstall(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 4)
			i965_irq_postinstall(dev_priv);
		else
			i915_irq_postinstall(dev_priv);
	} else {
		if (HAS_MEMORY_IRQ_STATUS(dev_priv))
			vf_mem_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			dg1_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 11)
			gen11_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 8)
			gen8_irq_postinstall(dev_priv);
		else
			ilk_irq_postinstall(dev_priv);
	}
}

/**
 * intel_irq_install - enables the hardware interrupt
 * @dev_priv: i915 device instance
 *
 * This function enables the hardware interrupt handling, but leaves the hotplug
 * handling still disabled. It is called after intel_irq_init().
 *
 * In the driver load and resume code we need working interrupts in a few places
 * but don't want to deal with the hassle of concurrent probe and hotplug
 * workers. Hence the split into this two-stage approach.
 */
int intel_irq_install(struct drm_i915_private *dev_priv)
{
	int irq = to_pci_dev(dev_priv->drm.dev)->irq;
	int ret;

	/*
	 * We enable some interrupt sources in our postinstall hooks, so mark
	 * interrupts as enabled _before_ actually enabling them to avoid
	 * special cases in our ordering checks.
	 */
	dev_priv->irqs_enabled = true;

	intel_irq_reset(dev_priv);

	ret = request_irq(irq, intel_irq_handler(dev_priv),
			  IRQF_SHARED, DRIVER_NAME, dev_priv);
	if (ret < 0) {
		dev_priv->irqs_enabled = false;
		return ret;
	}

	intel_irq_postinstall(dev_priv);

	return ret;
}

/**
 * intel_irq_uninstall - finalizes all irq handling
 * @dev_priv: i915 device instance
 *
 * This stops interrupt and hotplug handling and unregisters and frees all
 * resources acquired in the init functions.
 */
void intel_irq_uninstall(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = dev_priv->display;
	int irq = to_pci_dev(dev_priv->drm.dev)->irq;

	if (drm_WARN_ON(&dev_priv->drm, !dev_priv->irqs_enabled))
		return;

	intel_irq_reset(dev_priv);

	free_irq(irq, dev_priv);

	intel_hpd_cancel_work(display);
	dev_priv->irqs_enabled = false;
}

/**
 * intel_irq_suspend - Suspend interrupts
 * @i915: i915 device instance
 *
 * This function is used to disable interrupts at runtime.
 */
void intel_irq_suspend(struct drm_i915_private *i915)
{
	intel_irq_reset(i915);
	i915->irqs_enabled = false;
	intel_synchronize_irq(i915);
}

/**
 * intel_irq_resume - Resume interrupts
 * @i915: i915 device instance
 *
 * This function is used to enable interrupts at runtime.
 */
void intel_irq_resume(struct drm_i915_private *i915)
{
	i915->irqs_enabled = true;
	intel_irq_reset(i915);
	intel_irq_postinstall(i915);
}

bool intel_irqs_enabled(struct drm_i915_private *dev_priv)
{
	return dev_priv->irqs_enabled;
}

void intel_synchronize_irq(struct drm_i915_private *i915)
{
	synchronize_irq(to_pci_dev(i915->drm.dev)->irq);
}

void intel_synchronize_hardirq(struct drm_i915_private *i915)
{
	synchronize_hardirq(to_pci_dev(i915->drm.dev)->irq);
}
