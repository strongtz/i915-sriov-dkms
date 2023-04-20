// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_CX0_REG_DEFS_H__
#define __INTEL_CX0_REG_DEFS_H__

/* C10 Vendor Registers */
#define PHY_C10_VDR_PLL(idx)            (0xC00 + (idx))
#define  C10_PLL0_FRACEN                REG_BIT8(4)
#define  C10_PLL3_MULTIPLIERH_MASK      REG_GENMASK8(3, 0)
#define  C10_PLL15_TXCLKDIV_MASK        REG_GENMASK8(2, 0)
#define PHY_C10_VDR_CMN(idx)            (0xC20 + (idx))
#define  C10_CMN0_DP_VAL                0x21
#define  C10_CMN3_TXVBOOST_MASK         REG_GENMASK8(7, 5)
#define  C10_CMN3_TXVBOOST(val)         REG_FIELD_PREP8(C10_CMN3_TXVBOOST_MASK, val)
#define PHY_C10_VDR_TX(idx)             (0xC30 + (idx))
#define  C10_TX0_VAL                    0x10
#define PHY_C10_VDR_CONTROL(idx)        (0xC70 + (idx) - 1)
#define  C10_VDR_CTRL_MSGBUS_ACCESS     REG_BIT8(2)
#define  C10_VDR_CTRL_MASTER_LANE       REG_BIT8(1)
#define  C10_VDR_CTRL_UPDATE_CFG        REG_BIT8(0)
#define PHY_C10_VDR_CUSTOM_WIDTH        0xD02

#endif /* __INTEL_CX0_REG_DEFS_H__ */
