/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _ABI_IOV_ACTIONS_MMIO_ABI_H_
#define _ABI_IOV_ACTIONS_MMIO_ABI_H_

#include "iov_messages_abi.h"

/**
 * DOC: IOV MMIO Opcodes
 *
 *  + _`IOV_OPCODE_VF2PF_MMIO_HANDSHAKE` = 0x01
 *  + _`IOV_OPCODE_VF2PF_MMIO_GET_RUNTIME` = 0x10
 */

/**
 * DOC: VF2PF_MMIO_HANDSHAKE
 *
 * This VF2PF MMIO message is used by the VF to establish ABI version with the PF.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | MAGIC - see VF2GUC_MMIO_RELAY_SERVICE_                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | OPCODE = IOV_OPCODE_VF2PF_MMIO_HANDSHAKE_                    |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE_               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | **MAJOR** - requested major version of the VFPF interface    |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **MINOR** - requested minor version of the VFPF interface    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | **MAJOR** - agreed major version of the VFPF interface       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **MINOR** - agreed minor version of the VFPF interface       |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_OPCODE_VF2PF_MMIO_HANDSHAKE			0x01

#define VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_LEN		4u
#define VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_1_MAJOR	(0xffff << 16)
#define VF2PF_MMIO_HANDSHAKE_REQUEST_MSG_1_MINOR	(0xffff << 0)

#define VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_LEN		4u
#define VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_1_MAJOR	(0xffff << 16)
#define VF2PF_MMIO_HANDSHAKE_RESPONSE_MSG_1_MINOR	(0xffff << 0)

/**
 * DOC: VF2PF_MMIO_UPDATE_GGTT
 *
 * This VF2PF MMIO message is used to request the PF to update the GGTT mapping
 * using the PTE provided by the VF
 * If more than one PTE should be mapped, then the next PTEs are generated based
 * on provided PTE.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | MAGIC - see VF2GUC_MMIO_RELAY_SERVICE_                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | OPCODE = IOV_OPCODE_VF2PF_MMIO_UPDATE_GGTT_                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE_               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:12 | **OFFSET** - relative offset within VF's GGTT region         |
 *  |   |       | 0x00000 = VF GGTT BEGIN                                      |
 *  |   |       | 0x00001 = VF GGTT BEGIN + 4K                                 |
 *  |   |       | 0x00002 = VF GGTT BEGIN + 8K                                 |
 *  |   |       | 0x00003 = ...                                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 11:10 | **MODE** = PTE copy mode                                     |
 *  |   |       |                                                              |
 *  |   |       | Controls where additional PTEs are inserted (either after    |
 *  |   |       | first PTE0 or last PTEn) and how new PTEs are prepared       |
 *  |   |       | (either as exact copy of PTE0/PTEn or altered PTE0/PTEn with |
 *  |   |       | GPA` updated by 4K for consecutive GPA allocations).         |
 *  |   |       | Applicable only when %NUM_COPIES is non-zero!                |
 *  |   |       |                                                              |
 *  |   |       | 0 = **DUPLICATE** = duplicate PTE0                           |
 *  |   |       | 1 = **REPLICATE** = replicate PTE0 using GPA`                |
 *  |   |       | 2 = **DUPLICATE_LAST** = duplicate PTEn                      |
 *  |   |       | 3 = **REPLICATE_LAST** = replicate PTEn using GPA`           |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   9:0 | **NUM_COPIES** = number of PTEs to copy                      |
 *  |   |       |                                                              |
 *  |   |       | Allows to update additional GGTT pages using existing PTE.   |
 *  |   |       | New PTEs are prepared according to the %MODE.                |
 *  |   |       |                                                              |
 *  |   |       | 0 = no copies                                                |
 *  |   |       | ...                                                          |
 *  |   |       | N = update additional N pages                                |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **PTE_LO** - lower 32 bits of GGTT PTE0                      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **PTE_HI** - upper 32 bits of GGTT PTE0                      |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | MAGIC - see _VF2GUC_MMIO_RELAY_SERVICE                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  23:0 | **NUM_PTES** - number of PTEs entries updated                |
 *  +---+-------+--------------------------------------------------------------+
 */

#define IOV_OPCODE_VF2PF_MMIO_UPDATE_GGTT		0x2

#define VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_LEN		4u
#define VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_OFFSET	(0xfffff << 12)
#define VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_MODE	(0x3 << 10)
#define   MMIO_UPDATE_GGTT_MODE_DUPLICATE		0u
#define   MMIO_UPDATE_GGTT_MODE_REPLICATE		1u
#define   MMIO_UPDATE_GGTT_MODE_DUPLICATE_LAST		2u
#define   MMIO_UPDATE_GGTT_MODE_REPLICATE_LAST		3u
#define VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_1_NUM_COPIES	(0x3ff << 0)
#define VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_2_PTE_LO	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2PF_MMIO_UPDATE_GGTT_REQUEST_MSG_3_PTE_HI	GUC_HXG_REQUEST_MSG_n_DATAn
#define   MMIO_UPDATE_GGTT_MAX_PTES			1u

#define VF2PF_MMIO_UPDATE_GGTT_RESPONSE_MSG_LEN		1u
#define VF2PF_MMIO_UPDATE_GGTT_RESPONSE_MSG_1_NUM_PTES	(0xffffff << 0)

/**
 * DOC: VF2PF_MMIO_GET_RUNTIME
 *
 * This opcode can be used by VFs to request values of some runtime registers
 * (fuses) that are not directly available for VFs.
 *
 * Only registers that are on the allow-list maintained by the PF are available.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | MAGIC - see VF2GUC_MMIO_RELAY_SERVICE_                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | OPCODE = IOV_OPCODE_VF2PF_MMIO_GET_RUNTIME_                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE_               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **OFFSET1** - offset of register1 (can't be zero)            |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **OFFSET2** - offset of register2 (or zero)                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **OFFSET3** - offset of register3 (or zero)                  |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | MAGIC - see _VF2GUC_MMIO_RELAY_SERVICE                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  23:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VALUE1** - value of the register1                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **VALUE2** - value of the register2 (or zero)                |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **VALUE3** - value of the register3 (or zero)                |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_OPCODE_VF2PF_MMIO_GET_RUNTIME		0x10

#define VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_LEN		4u
#define VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_n_OFFSETn	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2PF_MMIO_GET_RUNTIME_REQUEST_MSG_NUM_OFFSET	3u

#define VF2PF_MMIO_GET_RUNTIME_RESPONSE_MSG_LEN		4u
#define VF2PF_MMIO_GET_RUNTIME_RESPONSE_MSG_n_VALUEn	GUC_HXG_RESPONSE_MSG_n_DATAn
#define VF2PF_MMIO_GET_RUNTIME_RESPONSE_MSG_NUM_VALUE	3u

#endif /* _ABI_IOV_ACTIONS_MMIO_ABI_H_ */
