/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_regs.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_fbc.h"
#include "intel_fifo_underrun.h"
#include "intel_pch_display.h"

/**
 * DOC: fifo underrun handling
 *
 * The i915 driver checks for display fifo underruns using the interrupt signals
 * provided by the hardware. This is enabled by default and fairly useful to
 * debug display issues, especially watermark settings.
 *
 * If an underrun is detected this is logged into dmesg. To avoid flooding logs
 * and occupying the cpu underrun interrupts are disabled after the first
 * occurrence until the next modeset on a given pipe.
 *
 * Note that underrun detection on gmch platforms is a bit more ugly since there
 * is no interrupt (despite that the signalling bit is in the PIPESTAT pipe
 * interrupt register). Also on some other platforms underrun interrupts are
 * shared, which means that if we detect an underrun we need to disable underrun
 * reporting on all pipes.
 *
 * The code also supports underrun detection on the PCH transcoder.
 */

static bool ivb_can_enable_err_int(struct intel_display *display)
{
	struct intel_crtc *crtc;
	enum pipe pipe;

	lockdep_assert_held(&display->irq.lock);

	for_each_pipe(display, pipe) {
		crtc = intel_crtc_for_pipe(display, pipe);

		if (crtc->cpu_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static bool cpt_can_enable_serr_int(struct intel_display *display)
{
	enum pipe pipe;
	struct intel_crtc *crtc;

	lockdep_assert_held(&display->irq.lock);

	for_each_pipe(display, pipe) {
		crtc = intel_crtc_for_pipe(display, pipe);

		if (crtc->pch_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static void i9xx_check_fifo_underruns(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	i915_reg_t reg = PIPESTAT(display, crtc->pipe);
	u32 enable_mask;

	lockdep_assert_held(&display->irq.lock);

	if ((intel_de_read(display, reg) & PIPE_FIFO_UNDERRUN_STATUS) == 0)
		return;

	enable_mask = i915_pipestat_enable_mask(display, crtc->pipe);
	intel_de_write(display, reg, enable_mask | PIPE_FIFO_UNDERRUN_STATUS);
	intel_de_posting_read(display, reg);

	trace_intel_cpu_fifo_underrun(display, crtc->pipe);
	drm_err(display->drm, "pipe %c underrun\n", pipe_name(crtc->pipe));
}

static void i9xx_set_fifo_underrun_reporting(struct intel_display *display,
					     enum pipe pipe,
					     bool enable, bool old)
{
	i915_reg_t reg = PIPESTAT(display, pipe);

	lockdep_assert_held(&display->irq.lock);

	if (enable) {
		u32 enable_mask = i915_pipestat_enable_mask(display, pipe);

		intel_de_write(display, reg,
			       enable_mask | PIPE_FIFO_UNDERRUN_STATUS);
		intel_de_posting_read(display, reg);
	} else {
		if (old && intel_de_read(display, reg) & PIPE_FIFO_UNDERRUN_STATUS)
			drm_err(display->drm, "pipe %c underrun\n",
				pipe_name(pipe));
	}
}

static void ilk_set_fifo_underrun_reporting(struct intel_display *display,
					    enum pipe pipe, bool enable)
{
	u32 bit = (pipe == PIPE_A) ?
		DE_PIPEA_FIFO_UNDERRUN : DE_PIPEB_FIFO_UNDERRUN;

	if (enable)
		ilk_enable_display_irq(display, bit);
	else
		ilk_disable_display_irq(display, bit);
}

static void ivb_check_fifo_underruns(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pipe = crtc->pipe;
	u32 err_int = intel_de_read(display, GEN7_ERR_INT);

	lockdep_assert_held(&display->irq.lock);

	if ((err_int & ERR_INT_FIFO_UNDERRUN(pipe)) == 0)
		return;

	intel_de_write(display, GEN7_ERR_INT, ERR_INT_FIFO_UNDERRUN(pipe));
	intel_de_posting_read(display, GEN7_ERR_INT);

	trace_intel_cpu_fifo_underrun(display, pipe);
	drm_err(display->drm, "fifo underrun on pipe %c\n", pipe_name(pipe));
}

static void ivb_set_fifo_underrun_reporting(struct intel_display *display,
					    enum pipe pipe, bool enable,
					    bool old)
{
	if (enable) {
		intel_de_write(display, GEN7_ERR_INT,
			       ERR_INT_FIFO_UNDERRUN(pipe));

		if (!ivb_can_enable_err_int(display))
			return;

		ilk_enable_display_irq(display, DE_ERR_INT_IVB);
	} else {
		ilk_disable_display_irq(display, DE_ERR_INT_IVB);

		if (old &&
		    intel_de_read(display, GEN7_ERR_INT) & ERR_INT_FIFO_UNDERRUN(pipe)) {
			drm_err(display->drm,
				"uncleared fifo underrun on pipe %c\n",
				pipe_name(pipe));
		}
	}
}

static void bdw_set_fifo_underrun_reporting(struct intel_display *display,
					    enum pipe pipe, bool enable)
{
	if (enable)
		bdw_enable_pipe_irq(display, pipe, GEN8_PIPE_FIFO_UNDERRUN);
	else
		bdw_disable_pipe_irq(display, pipe, GEN8_PIPE_FIFO_UNDERRUN);
}

static void ibx_set_fifo_underrun_reporting(struct intel_display *display,
					    enum pipe pch_transcoder,
					    bool enable)
{
	u32 bit = (pch_transcoder == PIPE_A) ?
		SDE_TRANSA_FIFO_UNDER : SDE_TRANSB_FIFO_UNDER;

	if (enable)
		ibx_enable_display_interrupt(display, bit);
	else
		ibx_disable_display_interrupt(display, bit);
}

static void cpt_check_pch_fifo_underruns(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	enum pipe pch_transcoder = crtc->pipe;
	u32 serr_int = intel_de_read(display, SERR_INT);

	lockdep_assert_held(&display->irq.lock);

	if ((serr_int & SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder)) == 0)
		return;

	intel_de_write(display, SERR_INT,
		       SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder));
	intel_de_posting_read(display, SERR_INT);

	trace_intel_pch_fifo_underrun(display, pch_transcoder);
	drm_err(display->drm, "pch fifo underrun on pch transcoder %c\n",
		pipe_name(pch_transcoder));
}

static void cpt_set_fifo_underrun_reporting(struct intel_display *display,
					    enum pipe pch_transcoder,
					    bool enable, bool old)
{
	if (enable) {
		intel_de_write(display, SERR_INT,
			       SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder));

		if (!cpt_can_enable_serr_int(display))
			return;

		ibx_enable_display_interrupt(display, SDE_ERROR_CPT);
	} else {
		ibx_disable_display_interrupt(display, SDE_ERROR_CPT);

		if (old && intel_de_read(display, SERR_INT) &
		    SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder)) {
			drm_err(display->drm,
				"uncleared pch fifo underrun on pch transcoder %c\n",
				pipe_name(pch_transcoder));
		}
	}
}

static bool __intel_set_cpu_fifo_underrun_reporting(struct intel_display *display,
						    enum pipe pipe, bool enable)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
	bool old;

	lockdep_assert_held(&display->irq.lock);

	old = !crtc->cpu_fifo_underrun_disabled;
	crtc->cpu_fifo_underrun_disabled = !enable;

	if (HAS_GMCH(display))
		i9xx_set_fifo_underrun_reporting(display, pipe, enable, old);
	else if (display->platform.ironlake || display->platform.sandybridge)
		ilk_set_fifo_underrun_reporting(display, pipe, enable);
	else if (DISPLAY_VER(display) == 7)
		ivb_set_fifo_underrun_reporting(display, pipe, enable, old);
	else if (DISPLAY_VER(display) >= 8)
		bdw_set_fifo_underrun_reporting(display, pipe, enable);

	return old;
}

/**
 * intel_set_cpu_fifo_underrun_reporting - set cpu fifo underrun reporting state
 * @display: display device instance
 * @pipe: (CPU) pipe to set state for
 * @enable: whether underruns should be reported or not
 *
 * This function sets the fifo underrun state for @pipe. It is used in the
 * modeset code to avoid false positives since on many platforms underruns are
 * expected when disabling or enabling the pipe.
 *
 * Notice that on some platforms disabling underrun reports for one pipe
 * disables for all due to shared interrupts. Actual reporting is still per-pipe
 * though.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_cpu_fifo_underrun_reporting(struct intel_display *display,
					   enum pipe pipe, bool enable)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&display->irq.lock, flags);
	ret = __intel_set_cpu_fifo_underrun_reporting(display, pipe, enable);
	spin_unlock_irqrestore(&display->irq.lock, flags);

	return ret;
}

/**
 * intel_set_pch_fifo_underrun_reporting - set PCH fifo underrun reporting state
 * @display: display device instance
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 * @enable: whether underruns should be reported or not
 *
 * This function makes us disable or enable PCH fifo underruns for a specific
 * PCH transcoder. Notice that on some PCHs (e.g. CPT/PPT), disabling FIFO
 * underrun reporting for one transcoder may also disable all the other PCH
 * error interruts for the other transcoders, due to the fact that there's just
 * one interrupt mask/enable bit for all the transcoders.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_pch_fifo_underrun_reporting(struct intel_display *display,
					   enum pipe pch_transcoder,
					   bool enable)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pch_transcoder);
	unsigned long flags;
	bool old;

	/*
	 * NOTE: Pre-LPT has a fixed cpu pipe -> pch transcoder mapping, but LPT
	 * has only one pch transcoder A that all pipes can use. To avoid racy
	 * pch transcoder -> pipe lookups from interrupt code simply store the
	 * underrun statistics in crtc A. Since we never expose this anywhere
	 * nor use it outside of the fifo underrun code here using the "wrong"
	 * crtc on LPT won't cause issues.
	 */

	spin_lock_irqsave(&display->irq.lock, flags);

	old = !crtc->pch_fifo_underrun_disabled;
	crtc->pch_fifo_underrun_disabled = !enable;

	if (HAS_PCH_IBX(display))
		ibx_set_fifo_underrun_reporting(display,
						pch_transcoder,
						enable);
	else
		cpt_set_fifo_underrun_reporting(display,
						pch_transcoder,
						enable, old);

	spin_unlock_irqrestore(&display->irq.lock, flags);
	return old;
}

/**
 * intel_cpu_fifo_underrun_irq_handler - handle CPU fifo underrun interrupt
 * @display: display device instance
 * @pipe: (CPU) pipe to set state for
 *
 * This handles a CPU fifo underrun interrupt, generating an underrun warning
 * into dmesg if underrun reporting is enabled and then disables the underrun
 * interrupt to avoid an irq storm.
 */
void intel_cpu_fifo_underrun_irq_handler(struct intel_display *display,
					 enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);

	/* We may be called too early in init, thanks BIOS! */
	if (crtc == NULL)
		return;

	/* GMCH can't disable fifo underruns, filter them. */
	if (HAS_GMCH(display) &&
	    crtc->cpu_fifo_underrun_disabled)
		return;

	if (intel_set_cpu_fifo_underrun_reporting(display, pipe, false)) {
		trace_intel_cpu_fifo_underrun(display, pipe);

		drm_err(display->drm, "CPU pipe %c FIFO underrun\n", pipe_name(pipe));
	}

	intel_fbc_handle_fifo_underrun_irq(display);
}

/**
 * intel_pch_fifo_underrun_irq_handler - handle PCH fifo underrun interrupt
 * @display: display device instance
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 *
 * This handles a PCH fifo underrun interrupt, generating an underrun warning
 * into dmesg if underrun reporting is enabled and then disables the underrun
 * interrupt to avoid an irq storm.
 */
void intel_pch_fifo_underrun_irq_handler(struct intel_display *display,
					 enum pipe pch_transcoder)
{
	if (intel_set_pch_fifo_underrun_reporting(display, pch_transcoder,
						  false)) {
		trace_intel_pch_fifo_underrun(display, pch_transcoder);
		drm_err(display->drm, "PCH transcoder %c FIFO underrun\n",
			pipe_name(pch_transcoder));
	}
}

/**
 * intel_check_cpu_fifo_underruns - check for CPU fifo underruns immediately
 * @display: display device instance
 *
 * Check for CPU fifo underruns immediately. Useful on IVB/HSW where the shared
 * error interrupt may have been disabled, and so CPU fifo underruns won't
 * necessarily raise an interrupt, and on GMCH platforms where underruns never
 * raise an interrupt.
 */
void intel_check_cpu_fifo_underruns(struct intel_display *display)
{
	struct intel_crtc *crtc;

	spin_lock_irq(&display->irq.lock);

	for_each_intel_crtc(display->drm, crtc) {
		if (crtc->cpu_fifo_underrun_disabled)
			continue;

		if (HAS_GMCH(display))
			i9xx_check_fifo_underruns(crtc);
		else if (DISPLAY_VER(display) == 7)
			ivb_check_fifo_underruns(crtc);
	}

	spin_unlock_irq(&display->irq.lock);
}

/**
 * intel_check_pch_fifo_underruns - check for PCH fifo underruns immediately
 * @display: display device instance
 *
 * Check for PCH fifo underruns immediately. Useful on CPT/PPT where the shared
 * error interrupt may have been disabled, and so PCH fifo underruns won't
 * necessarily raise an interrupt.
 */
void intel_check_pch_fifo_underruns(struct intel_display *display)
{
	struct intel_crtc *crtc;

	spin_lock_irq(&display->irq.lock);

	for_each_intel_crtc(display->drm, crtc) {
		if (crtc->pch_fifo_underrun_disabled)
			continue;

		if (HAS_PCH_CPT(display))
			cpt_check_pch_fifo_underruns(crtc);
	}

	spin_unlock_irq(&display->irq.lock);
}

void intel_init_fifo_underrun_reporting(struct intel_display *display,
					struct intel_crtc *crtc,
					bool enable)
{
	crtc->cpu_fifo_underrun_disabled = !enable;

	/*
	 * We track the PCH trancoder underrun reporting state
	 * within the crtc. With crtc for pipe A housing the underrun
	 * reporting state for PCH transcoder A, crtc for pipe B housing
	 * it for PCH transcoder B, etc. LPT-H has only PCH transcoder A,
	 * and marking underrun reporting as disabled for the non-existing
	 * PCH transcoders B and C would prevent enabling the south
	 * error interrupt (see cpt_can_enable_serr_int()).
	 */
	if (intel_has_pch_trancoder(display, crtc->pipe))
		crtc->pch_fifo_underrun_disabled = !enable;
}
