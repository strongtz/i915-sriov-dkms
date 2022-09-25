/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _ABI_GUC_ACTIONS_VF_ABI_H
#define _ABI_GUC_ACTIONS_VF_ABI_H

#include "guc_communication_mmio_abi.h"
#include "guc_communication_ctb_abi.h"

/**
 * DOC: VF2GUC_MATCH_VERSION
 *
 * This action is used to match VF interface version used by VF and GuC.
 *
 * This action must be sent over MMIO.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_MATCH_VERSION` = 0x5500         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:24 | **BRANCH** - branch ID of the VF interface                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | **MAJOR** - major version of the VF interface                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:8 | **MINOR** - minor version of the VF interface                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **MBZ**                                                      |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:24 | **BRANCH** - branch ID of the VF interface                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | **MAJOR** - major version of the VF interface                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:8 | **MINOR** - minor version of the VF interface                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **PATCH** - patch version of the VF interface                |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_MATCH_VERSION			0x5500

#define VF2GUC_MATCH_VERSION_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_BRANCH	(0xff << 24)
#define   GUC_VERSION_BRANCH_ANY			0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MAJOR	(0xff << 16)
#define   GUC_VERSION_MAJOR_ANY				0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MINOR	(0xff << 8)
#define   GUC_VERSION_MINOR_ANY				0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MBZ		(0xff << 0)

#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_LEN		(GUC_HXG_RESPONSE_MSG_MIN_LEN + 1u)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_BRANCH	(0xff << 24)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MAJOR	(0xff << 16)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MINOR	(0xff << 8)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_PATCH	(0xff << 0)

/**
 * DOC: VF2GUC_VF_RESET
 *
 * This action is used by VF to reset GuC's VF state.
 *
 * This message must be sent as `MMIO HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_VF_RESET` = 0x5507              |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_VF_RESET			0x5507

#define VF2GUC_VF_RESET_REQUEST_MSG_LEN			GUC_HXG_REQUEST_MSG_MIN_LEN
#define VF2GUC_VF_RESET_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0

#define VF2GUC_VF_RESET_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define VF2GUC_VF_RESET_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: VF2GUC_QUERY_SINGLE_KLV
 *
 * This action is used by VF to query value of the single KLV data.
 *
 * This action must be sent over MMIO.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_QUERY_SINGLE_KLV` = 0x5509      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **KEY** - key for which value is requested                   |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **LENGTH** - length of data in dwords                        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VALUE32** - bits 31:0 of value if **LENGTH** >= 1          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **VALUE64** - bits 63:32 of value if **LENGTH** >= 2         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **VALUE96** - bits 95:64 of value if **LENGTH** >= 3         |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_QUERY_SINGLE_KLV		0x5509

#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_0_MBZ	GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_1_MBZ	(0xffff << 16)
#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_1_KEY	(0xffff << 0)

#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MIN_LEN	GUC_HXG_RESPONSE_MSG_MIN_LEN
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MAX_LEN	(GUC_HXG_RESPONSE_MSG_MIN_LEN + 3u)
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_MBZ	(0xfff << 16)
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_LENGTH	(0xffff << 0)
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_1_VALUE32	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_2_VALUE64	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_3_VALUE96	GUC_HXG_REQUEST_MSG_n_DATAn

/**
 * DOC: VF2GUC_RELAY_TO_PF
 *
 * The `VF2GUC_RELAY_TO_PF`_ message is used to send VF/PF messages to the PF.
 *
 * This message must be sent over CTB.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_ or GUC_HXG_TYPE_FAST_REQUEST_   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_RELAY_TO_PF` = 0x5103           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **RELAY_DATA1** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | **RELAY_DATAx** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_RELAY_TO_PF			0x5103

#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_MAX_LEN		(VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN + 60u)
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_1_RELAY_ID	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_n_RELAY_DATAx	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA	60u

/**
 * DOC: GUC2VF_RELAY_FROM_PF
 *
 * The `GUC2VF_RELAY_FROM_PF`_ message is used by GuC to forward VF/PF messages
 * received from the PF.
 *
 * This message must be sent over CTB.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2VF_RELAY_FROM_PF` = 0x5102         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **RELAY_DATA1** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | **RELAY_DATAx** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2VF_RELAY_FROM_PF			0x5102

#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 1u)
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN		(GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN + 60u)
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_n_RELAY_DATAx	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_NUM_RELAY_DATA	60u

/**
 * DOC: VF2GUC_MMIO_RELAY_SERVICE
 *
 * The VF2GUC_MMIO_RELAY_SERVICE action allows to send early MMIO VF/PF messages
 * from the VF to the PF.
 *
 * Note that support for the sending such messages to the PF is not guaranteed
 * and might be disabled or blocked in the future releases.
 *
 * The value of **MAGIC** used in the GUC_HXG_TYPE_REQUEST_ shall be generated
 * by the VF and value of **MAGIC** included in GUC_HXG_TYPE_RESPONSE_SUCCESS_
 * shall be the same.
 *
 * In case of GUC_HXG_TYPE_RESPONSE_FAILURE_, **MAGIC** shall be encoded in upper
 * bits of **HINT** field.
 *
 * This action may take longer time to completion and VFs should expect intermediate
 * `HXG Busy`_ response message.
 *
 * This action is only available over MMIO.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | **MAGIC** - MMIO VF/PF message magic number (like CRC)       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | **OPCODE** - MMIO VF/PF message opcode                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE` = 0x5005    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **DATA1** - optional MMIO VF/PF payload data (or zero)       |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **DATA2** - optional MMIO VF/PF payload data (or zero)       |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **DATA3** - optional MMIO VF/PF payload data (or zero)       |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | **MAGIC** - must match value from the REQUEST                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  23:0 | **DATA0** - MMIO VF/PF response data                         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **DATA1** - MMIO VF/PF response data (or zero)               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **DATA2** - MMIO VF/PF response data (or zero)               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **DATA3** - MMIO VF/PF response data (or zero)               |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_MMIO_RELAY_SERVICE		0x5005

#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MIN_LEN	(GUC_HXG_REQUEST_MSG_MIN_LEN)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_MAX_LEN	(GUC_HXG_REQUEST_MSG_MIN_LEN + 3u)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_MAGIC	(0xf << 24)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_0_OPCODE	(0xff << 16)
#define VF2GUC_MMIO_RELAY_SERVICE_REQUEST_MSG_n_DATAn	GUC_HXG_REQUEST_MSG_n_DATAn

#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MIN_LEN	(GUC_HXG_RESPONSE_MSG_MIN_LEN)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_MAX_LEN	(GUC_HXG_RESPONSE_MSG_MIN_LEN + 3u)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_0_MAGIC	(0xf << 24)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_0_DATA0	(0xffffff << 0)
#define VF2GUC_MMIO_RELAY_SERVICE_RESPONSE_MSG_n_DATAn	GUC_HXG_RESPONSE_MSG_n_DATAn

#endif /* _ABI_GUC_ACTIONS_VF_ABI_H */
