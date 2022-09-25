/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _ABI_IOV_ACTIONS_SELFTEST_ABI_H_
#define _ABI_IOV_ACTIONS_SELFTEST_ABI_H_

#include "iov_actions_debug_abi.h"

/**
 * DOC: IOV_ACTION_SELFTEST_RELAY
 *
 * This special `IOV Action`_ is used to selftest `IOV communication`_.
 *
 * SELFTEST_RELAY_OPCODE_NOP_ will return no data.
 * SELFTEST_RELAY_OPCODE_ECHO_ will return same data as received.
 * SELFTEST_RELAY_OPCODE_FAIL_ will always fail with error.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_ or GUC_HXG_TYPE_FAST_REQUEST_   |
 *  |   |       | or GUC_HXG_TYPE_EVENT_                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | **OPCODE**                                                   |
 *  |   |       |    - _`SELFTEST_RELAY_OPCODE_NOP` = 0x0                      |
 *  |   |       |    - _`SELFTEST_RELAY_OPCODE_ECHO` = 0xE                     |
 *  |   |       |    - _`SELFTEST_RELAY_OPCODE_FAIL` = 0xF                     |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`IOV_ACTION_SELFTEST_RELAY`                        |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|  31:0 | **PAYLOAD** optional                                         |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|  31:0 | DATAn = only for **OPCODE** SELFTEST_RELAY_OPCODE_ECHO       |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_ACTION_SELFTEST_RELAY	(IOV_ACTION_DEBUG_ONLY_START + 1)
#define   SELFTEST_RELAY_OPCODE_NOP		0x0
#define   SELFTEST_RELAY_OPCODE_ECHO		0xE
#define   SELFTEST_RELAY_OPCODE_FAIL		0xF

/**
 * DOC: VF2PF_PF_ST_ACTION
 *
 * This `IOV Message`_ is used by VF to initiate some selftest action on the PF.
 *
 * See `IOV SELFTEST Opcodes`_ for available selftest operations.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = **OPCODE** - see `IOV SELFTEST Opcodes`_             |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = VF2PF_PF_ST_ACTION_ = TBD                           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **ST_DATA1** - VF/PF selftest message data           |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | DATAn = **ST_DATAn** - VF/PF selftest message data           |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **RET_DATA1** - PF/VF selftest return data           |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | DATAn = **RET_DATAn** - PF/VF selftest return data           |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_ACTION_VF2PF_PF_ST_ACTION		(IOV_ACTION_DEBUG_ONLY_START + 2)

#define VF2PF_PF_ST_ACTION_REQUEST_MSG_MIN_LEN		GUC_HXG_MSG_MIN_LEN
#define VF2PF_PF_ST_ACTION_REQUEST_MSG_MAX_LEN		20 // FIXME RELAY_PAYLOAD_MAX_SIZE
#define VF2PF_PF_ST_ACTION_REQUEST_MSG_0_OPCODE		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2PF_PF_ST_ACTION_REQUEST_MSG_n_ST_DATAn	GUC_HXG_RESPONSE_MSG_n_DATAn

#define VF2PF_PF_ST_ACTION_RESPONSE_MSG_MIN_LEN		GUC_HXG_MSG_MIN_LEN
#define VF2PF_PF_ST_ACTION_RESPONSE_MSG_MAX_LEN		20 // FIXME RELAY_PAYLOAD_MAX_SIZE
#define VF2PF_PF_ST_ACTION_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2PF_PF_ST_ACTION_RESPONSE_MSG_n_RET_DATAn	GUC_HXG_RESPONSE_MSG_n_DATAn

/**
 * DOC: IOV SELFTEST Opcodes
 *
 *  - IOV_OPCODE_ST_GET_GGTT_PTE_ = 1
 */

/**
 * DOC: IOV_OPCODE_ST_GET_GGTT_PTE
 *
 * Action to get value of PTE, for a given GGTT address, from PF.
 *
 * See VF2PF_PF_ST_ACTION_.
 *
 * Note: GGTT address must be aligned to 4K, or action will fail
 * with IOV_ERROR_INVALID_ARGUMENT.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | OPCODE = IOV_OPCODE_ST_GET_GGTT_PTE_ = 1                     |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = VF2PF_PF_ST_ACTION_ = TBD                           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **ADDRESS_LO** - lower bits of GGTT address          |
 *  |   |       |                          (aligned to 4K)                     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **ADDRESS_HI** - upper bits of GGTT address          |
 *  |   |       |                          (aligned to 4K)                     |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **PTE_LO** - lower bits of returned PTE              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **PTE_HI** - upper bits of returned PTE              |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_OPCODE_ST_GET_GGTT_PTE		0x1

#define VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_LEN		(VF2PF_PF_ST_ACTION_REQUEST_MSG_MIN_LEN + \
							 2u)
#define VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_1_ADDRESS_LO	VF2PF_PF_ST_ACTION_REQUEST_MSG_n_ST_DATAn
#define VF2PF_ST_GET_GGTT_PTE_REQUEST_MSG_2_ADDRESS_HI	VF2PF_PF_ST_ACTION_REQUEST_MSG_n_ST_DATAn

#define VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_LEN		(VF2PF_PF_ST_ACTION_RESPONSE_MSG_MIN_LEN + \
							 2u)
#define VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_1_PTE_LO	VF2PF_PF_ST_ACTION_RESPONSE_MSG_n_RET_DATAn
#define VF2PF_ST_GET_GGTT_PTE_RESPONSE_MSG_2_PTE_HI	VF2PF_PF_ST_ACTION_RESPONSE_MSG_n_RET_DATAn

#endif /* _ABI_IOV_ACTIONS_SELFTEST_ABI_H_ */
