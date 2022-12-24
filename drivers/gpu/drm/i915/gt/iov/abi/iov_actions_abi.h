/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _ABI_IOV_ACTIONS_ABI_H_
#define _ABI_IOV_ACTIONS_ABI_H_

#include "iov_messages_abi.h"

/**
 * DOC: IOV Actions
 *
 * TBD
 */

/**
 * DOC: VF2PF_HANDSHAKE
 *
 * This `IOV Message`_ is used by the VF to establish ABI version with the PF.
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
 *  |   |  15:0 | ACTION = _`IOV_ACTION_VF2PF_HANDSHAKE` = 0x0001              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | **MAJOR** - requested major version of the VFPF interface    |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **MINOR** - requested minor version of the VFPF interface    |
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
 *  | 1 | 31:16 | **MAJOR** - agreed major version of the VFPF interface       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **MINOR** - agreed minor version of the VFPF interface       |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_ACTION_VF2PF_HANDSHAKE			0x0001

#define VF2PF_HANDSHAKE_REQUEST_MSG_LEN			2u
#define VF2PF_HANDSHAKE_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR		(0xffff << 16)
#define VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR		(0xffff << 0)

#define VF2PF_HANDSHAKE_RESPONSE_MSG_LEN		2u
#define VF2PF_HANDSHAKE_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR		(0xffff << 16)
#define VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR		(0xffff << 0)

/**
 * DOC: VF2PF_QUERY_RUNTIME
 *
 * This `IOV Message`_ is used by the VF to query values of runtime registers.
 *
 * VF provides @START index to the requested register entry.
 * VF can use @LIMIT to limit number of returned register entries.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = **LIMIT** - limit number of returned entries         |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`IOV_ACTION_VF2PF_QUERY_RUNTIME` = 0x0101          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **START** - index of the first requested entry       |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = **COUNT** - number of entries included in response   |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **REMAINING** - number of remaining entries          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **REG_OFFSET** - offset of register[START]           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | DATA3 = **REG_VALUE** - value of register[START]             |
 *  +---+-------+--------------------------------------------------------------+
 *  |   |       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  |n-1|  31:0 | REG_OFFSET - offset of register[START + x]                   |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | REG_VALUE - value of register[START + x]                     |
 *  +---+-------+--------------------------------------------------------------+
 */
#define IOV_ACTION_VF2PF_QUERY_RUNTIME			0x0101

#define VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN		2u
#define VF2PF_QUERY_RUNTIME_REQUEST_MSG_0_LIMIT		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2PF_QUERY_RUNTIME_REQUEST_MSG_1_START		GUC_HXG_REQUEST_MSG_n_DATAn

#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN	(GUC_HXG_MSG_MIN_LEN + 1u)
#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MAX_LEN	20 // FIXME RELAY_PAYLOAD_MAX_SIZE
#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_0_COUNT	GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_1_REMAINING	GUC_HXG_RESPONSE_MSG_n_DATAn
#define VF2PF_QUERY_RUNTIME_RESPONSE_DATAn_REG_OFFSETx	GUC_HXG_RESPONSE_MSG_n_DATAn
#define VF2PF_QUERY_RUNTIME_RESPONSE_DATAn_REG_VALUEx	GUC_HXG_RESPONSE_MSG_n_DATAn

#endif /* _ABI_IOV_ACTIONS_ABI_H_ */
