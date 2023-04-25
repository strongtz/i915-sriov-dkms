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

#endif /* _ABI_IOV_ACTIONS_SELFTEST_ABI_H_ */
