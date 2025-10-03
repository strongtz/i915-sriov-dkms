/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DE_H__
#define __INTEL_DE_H__

#include "intel_display_core.h"
#include "intel_dmc_wl.h"
#include "intel_dsb.h"
#include "intel_uncore.h"
#include "intel_uncore_trace.h"

static inline struct intel_uncore *__to_uncore(struct intel_display *display)
{
	return to_intel_uncore(display->drm);
}

static inline u32
intel_de_read(struct intel_display *display, i915_reg_t reg)
{
	u32 val;

	intel_dmc_wl_get(display, reg);

	val = intel_uncore_read(__to_uncore(display), reg);

	intel_dmc_wl_put(display, reg);

	return val;
}

static inline u8
intel_de_read8(struct intel_display *display, i915_reg_t reg)
{
	u8 val;

	intel_dmc_wl_get(display, reg);

	val = intel_uncore_read8(__to_uncore(display), reg);

	intel_dmc_wl_put(display, reg);

	return val;
}

static inline u64
intel_de_read64_2x32(struct intel_display *display,
		     i915_reg_t lower_reg, i915_reg_t upper_reg)
{
	u64 val;

	intel_dmc_wl_get(display, lower_reg);
	intel_dmc_wl_get(display, upper_reg);

	val = intel_uncore_read64_2x32(__to_uncore(display), lower_reg,
				       upper_reg);

	intel_dmc_wl_put(display, upper_reg);
	intel_dmc_wl_put(display, lower_reg);

	return val;
}

static inline void
intel_de_posting_read(struct intel_display *display, i915_reg_t reg)
{
	intel_dmc_wl_get(display, reg);

	intel_uncore_posting_read(__to_uncore(display), reg);

	intel_dmc_wl_put(display, reg);
}

static inline void
intel_de_write(struct intel_display *display, i915_reg_t reg, u32 val)
{
	intel_dmc_wl_get(display, reg);

	intel_uncore_write(__to_uncore(display), reg, val);

	intel_dmc_wl_put(display, reg);
}

static inline u32
__intel_de_rmw_nowl(struct intel_display *display, i915_reg_t reg,
		    u32 clear, u32 set)
{
	return intel_uncore_rmw(__to_uncore(display), reg, clear, set);
}

static inline u32
intel_de_rmw(struct intel_display *display, i915_reg_t reg, u32 clear, u32 set)
{
	u32 val;

	intel_dmc_wl_get(display, reg);

	val = __intel_de_rmw_nowl(display, reg, clear, set);

	intel_dmc_wl_put(display, reg);

	return val;
}

static inline int
__intel_de_wait_for_register_nowl(struct intel_display *display,
				  i915_reg_t reg,
				  u32 mask, u32 value, unsigned int timeout_ms)
{
	return intel_wait_for_register(__to_uncore(display), reg, mask,
				       value, timeout_ms);
}

static inline int
__intel_de_wait_for_register_atomic_nowl(struct intel_display *display,
					 i915_reg_t reg,
					 u32 mask, u32 value,
					 unsigned int fast_timeout_us)
{
	return __intel_wait_for_register(__to_uncore(display), reg, mask,
					 value, fast_timeout_us, 0, NULL);
}

static inline int
intel_de_wait(struct intel_display *display, i915_reg_t reg,
	      u32 mask, u32 value, unsigned int timeout_ms)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = __intel_de_wait_for_register_nowl(display, reg, mask, value,
						timeout_ms);

	intel_dmc_wl_put(display, reg);

	return ret;
}

static inline int
intel_de_wait_fw(struct intel_display *display, i915_reg_t reg,
		 u32 mask, u32 value, unsigned int timeout_ms, u32 *out_value)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = intel_wait_for_register_fw(__to_uncore(display), reg, mask,
					 value, timeout_ms, out_value);

	intel_dmc_wl_put(display, reg);

	return ret;
}

static inline int
intel_de_wait_custom(struct intel_display *display, i915_reg_t reg,
		     u32 mask, u32 value,
		     unsigned int fast_timeout_us,
		     unsigned int slow_timeout_ms, u32 *out_value)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = __intel_wait_for_register(__to_uncore(display), reg, mask,
					value,
					fast_timeout_us, slow_timeout_ms, out_value);

	intel_dmc_wl_put(display, reg);

	return ret;
}

static inline int
intel_de_wait_for_set(struct intel_display *display, i915_reg_t reg,
		      u32 mask, unsigned int timeout_ms)
{
	return intel_de_wait(display, reg, mask, mask, timeout_ms);
}

static inline int
intel_de_wait_for_clear(struct intel_display *display, i915_reg_t reg,
			u32 mask, unsigned int timeout_ms)
{
	return intel_de_wait(display, reg, mask, 0, timeout_ms);
}

/*
 * Unlocked mmio-accessors, think carefully before using these.
 *
 * Certain architectures will die if the same cacheline is concurrently accessed
 * by different clients (e.g. on Ivybridge). Access to registers should
 * therefore generally be serialised, by either the dev_priv->uncore.lock or
 * a more localised lock guarding all access to that bank of registers.
 */
static inline u32
intel_de_read_fw(struct intel_display *display, i915_reg_t reg)
{
	u32 val;

	val = intel_uncore_read_fw(__to_uncore(display), reg);
	trace_i915_reg_rw(false, reg, val, sizeof(val), true);

	return val;
}

static inline void
intel_de_write_fw(struct intel_display *display, i915_reg_t reg, u32 val)
{
	trace_i915_reg_rw(true, reg, val, sizeof(val), true);
	intel_uncore_write_fw(__to_uncore(display), reg, val);
}

static inline u32
intel_de_read_notrace(struct intel_display *display, i915_reg_t reg)
{
	return intel_uncore_read_notrace(__to_uncore(display), reg);
}

static inline void
intel_de_write_notrace(struct intel_display *display, i915_reg_t reg, u32 val)
{
	intel_uncore_write_notrace(__to_uncore(display), reg, val);
}

static __always_inline void
intel_de_write_dsb(struct intel_display *display, struct intel_dsb *dsb,
		   i915_reg_t reg, u32 val)
{
	if (dsb)
		intel_dsb_reg_write(dsb, reg, val);
	else
		intel_de_write_fw(display, reg, val);
}

#endif /* __INTEL_DE_H__ */
