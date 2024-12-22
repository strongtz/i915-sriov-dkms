/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_IOV_REG_H__
#define __INTEL_IOV_REG_H__

/* ISR */
#define I915_VF_IRQ_STATUS 0x0
/* IIR */
#define I915_VF_IRQ_SOURCE 0x400
/* IMR */
#define I915_VF_IRQ_ENABLE 0x440

/*
 * VF registers, at offset 0x190000, are all accessible from PF after applying
 * stride of 0x1000 or 0x400 (depending on the platform)
 */
#define GEN12_VF_REGISTERS_STRIDE	0x1000
#define XEHPSDV_VF_REGISTERS_STRIDE	0x400

#define GEN12_VF_GFX_MSTR_IRQ(__vfid)	_MMIO(0x190010 + (__vfid) * GEN12_VF_REGISTERS_STRIDE)
#define XEHPSDV_VF_GFX_MSTR_IRQ(__vfid)	_MMIO(0x190010 + (__vfid) * XEHPSDV_VF_REGISTERS_STRIDE)

#endif /* __INTEL_IOV_REG_H__ */
