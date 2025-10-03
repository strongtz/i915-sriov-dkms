/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */
#ifndef _XE_REGS_H_
#define _XE_REGS_H_

#include "regs/xe_reg_defs.h"

#define SOC_BASE				0x280000

#define GU_CNTL_PROTECTED			XE_REG(0x10100C)
#define   DRIVERINT_FLR_DIS			REG_BIT(31)

#define GU_CNTL					XE_REG(0x101010)
#define   LMEM_INIT				REG_BIT(7)
#define   DRIVERFLR				REG_BIT(31)

#define XEHP_CLOCK_GATE_DIS			XE_REG(0x101014)
#define   SGSI_SIDECLK_DIS			REG_BIT(17)

#define GU_DEBUG				XE_REG(0x101018)
#define   DRIVERFLR_STATUS			REG_BIT(31)

#define VIRTUAL_CTRL_REG			XE_REG(0x10108c)
#define   GUEST_GTT_UPDATE_EN			REG_BIT(8)

#define XEHP_MTCFG_ADDR				XE_REG(0x101800)
#define   TILE_COUNT				REG_GENMASK(15, 8)

#define GGC					XE_REG(0x108040)
#define   GMS_MASK				REG_GENMASK(15, 8)
#define   GGMS_MASK				REG_GENMASK(7, 6)

#define DSMBASE					XE_REG(0x1080C0)
#define   BDSM_MASK				REG_GENMASK64(63, 20)

#define GSMBASE					XE_REG(0x108100)

#define STOLEN_RESERVED				XE_REG(0x1082c0)
#define   WOPCM_SIZE_MASK			REG_GENMASK64(9, 7)

#define MTL_RP_STATE_CAP			XE_REG(0x138000)

#define MTL_GT_RPA_FREQUENCY			XE_REG(0x138008)
#define MTL_GT_RPE_FREQUENCY			XE_REG(0x13800c)

#define MTL_MEDIAP_STATE_CAP			XE_REG(0x138020)
#define   MTL_RPN_CAP_MASK			REG_GENMASK(24, 16)
#define   MTL_RP0_CAP_MASK			REG_GENMASK(8, 0)

#define MTL_MPA_FREQUENCY			XE_REG(0x138028)
#define   MTL_RPA_MASK				REG_GENMASK(8, 0)

#define MTL_MPE_FREQUENCY			XE_REG(0x13802c)
#define   MTL_RPE_MASK				REG_GENMASK(8, 0)

#define VF_CAP_REG				XE_REG(0x1901f8, XE_REG_OPTION_VF)
#define   VF_CAP				REG_BIT(0)

#define PVC_RP_STATE_CAP			XE_REG(0x281014)

#endif
