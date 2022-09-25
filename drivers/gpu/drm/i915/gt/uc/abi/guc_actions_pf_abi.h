/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __GUC_ACTIONS_PF_ABI_H__
#define __GUC_ACTIONS_PF_ABI_H__

#include "guc_communication_ctb_abi.h"

/**
 * DOC: PF2GUC_UPDATE_VGT_POLICY
 *
 * This message is optionaly used by the PF to set `GuC VGT Policy KLVs`_.
 *
 * This message must be sent as `CTB HXG Message`_.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_UPDATE_VGT_POLICY` = 0x5502     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **CFG_ADDR_LO** - dword aligned GGTT offset that             |
 *  |   |       | represents the start of `GuC VGT Policy KLVs`_ list.         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **CFG_ADDR_HI** - upper 32 bits of above offset.             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **CFG_SIZE** - size (in dwords) of the config buffer         |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **COUNT** - number of KLVs successfully applied              |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_UPDATE_VGT_POLICY			0x5502

#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 3u)
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_1_CFG_ADDR_LO	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_2_CFG_ADDR_HI	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_3_CFG_SIZE		GUC_HXG_REQUEST_MSG_n_DATAn

#define PF2GUC_UPDATE_VGT_POLICY_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define PF2GUC_UPDATE_VGT_POLICY_RESPONSE_MSG_0_COUNT		GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: PF2GUC_UPDATE_VF_CFG
 *
 * The `PF2GUC_UPDATE_VF_CFG`_ message is used by PF to provision single VF in GuC.
 *
 * This message must be sent as `CTB HXG Message`_.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_UPDATE_VF_CFG` = 0x5503         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - identifier of the VF that the KLV                 |
 *  |   |       | configurations are being applied to                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **CFG_ADDR_LO** - dword aligned GGTT offset that represents  |
 *  |   |       | the start of a list of virtualization related KLV configs    |
 *  |   |       | that are to be applied to the VF.                            |
 *  |   |       | If this parameter is zero, the list is not parsed.           |
 *  |   |       | If full configs address parameter is zero and configs_size is|
 *  |   |       | zero associated VF config shall be reset to its default state|
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **CFG_ADDR_HI** - upper 32 bits of configs address.          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 4 |  31:0 | **CFG_SIZE** - size (in dwords) of the config buffer         |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **COUNT** - number of KLVs successfully applied              |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_UPDATE_VF_CFG			0x5503

#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 4u)
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_1_VFID		GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_2_CFG_ADDR_LO	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_3_CFG_ADDR_HI	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_4_CFG_SIZE	GUC_HXG_REQUEST_MSG_n_DATAn

#define PF2GUC_UPDATE_VF_CFG_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define PF2GUC_UPDATE_VF_CFG_RESPONSE_MSG_0_COUNT	GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: GUC2PF_RELAY_FROM_VF
 *
 * The `GUC2PF_RELAY_FROM_VF`_ message is used by the GuC to forward VF/PF IOV
 * messages received from the VF to the PF.
 *
 * This H2G message must be sent as `CTB HXG Message`_.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2PF_RELAY_FROM_VF` = 0x5100         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - source VF identifier                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **RELAY_DATA1** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | **RELAY_DATAx** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2PF_RELAY_FROM_VF			0x5100

#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN		(GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN + 60u)
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_3_RELAY_DATA1	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_n_RELAY_DATAx	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_NUM_RELAY_DATA	60u

/**
 * DOC: PF2GUC_RELAY_TO_VF
 *
 * The `PF2GUC_RELAY_TO_VF`_ message is used by the PF to send VF/PF IOV messages
 * to the VF.
 *
 * This action message must be sent over CTB as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = `GUC_HXG_TYPE_FAST_REQUEST`_                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_RELAY_TO_VF` = 0x5101           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - target VF identifier                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **RELAY_DATA1** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | **RELAY_DATAx** - VF/PF message payload data                 |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_RELAY_TO_VF			0x5101

#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 2u)
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN		(PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + 60u)
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID		GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_3_RELAY_DATA1	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_n_RELAY_DATAx	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA	60u

/**
 * DOC: GUC2PF_MMIO_RELAY_SERVICE
 *
 * The `GUC2PF_MMIO_RELAY_SERVICE`_ message is used by the GuC to forward data
 * from `VF2GUC_MMIO_RELAY_SERVICE`_ request message that was sent by the VF.
 *
 * To reply to `VF2GUC_MMIO_RELAY_SERVICE`_ request message PF must be either
 * `PF2GUC_MMIO_RELAY_SUCCESS`_ or `PF2GUC_MMIO_RELAY_FAILURE`_.
 *
 * This G2H message must be sent as `CTB HXG Message`_.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2PF_MMIO_RELAY_SERVICE` = 0x5006    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - identifier of the VF which sent this message      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 | 31:28 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | **MAGIC** - see `VF2GUC_MMIO_RELAY_SERVICE`_ request         |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | **OPCODE** - see `VF2GUC_MMIO_RELAY_SERVICE`_ request        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | MBZ                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **DATA1** - see `VF2GUC_MMIO_RELAY_SERVICE`_ request         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 4 |  31:0 | **DATA2** - see `VF2GUC_MMIO_RELAY_SERVICE`_ request         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 5 |  31:0 | **DATA3** - see `VF2GUC_MMIO_RELAY_SERVICE`_ request         |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2PF_MMIO_RELAY_SERVICE		0x5006

#define GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 5u)
#define GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_1_VFID	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_2_MAGIC	(0xf << 24)
#define GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_2_OPCODE	(0xff << 16)
#define GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_n_DATAx	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_MMIO_RELAY_SERVICE_EVENT_MSG_NUM_DATA	3u

/**
 * DOC: PF2GUC_MMIO_RELAY_SUCCESS
 *
 * The `PF2GUC_MMIO_RELAY_SUCCESS`_ message is used by the PF to send success
 * response data related to `VF2GUC_MMIO_RELAY_SERVICE`_ request message that
 * was received in `GUC2PF_MMIO_RELAY_SERVICE`_.
 *
 * This message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_FAST_REQUEST_                            |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_MMIO_RELAY_SUCCESS` = 0x5007    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - identifier of the VF where to send this reply     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 | 31:28 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | **MAGIC** - see `VF2GUC_MMIO_RELAY_SERVICE`_ response        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  23:0 | **DATA0** - see `VF2GUC_MMIO_RELAY_SERVICE`_ response        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **DATA1** - see `VF2GUC_MMIO_RELAY_SERVICE`_ response        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 4 |  31:0 | **DATA2** - see `VF2GUC_MMIO_RELAY_SERVICE`_ response        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 5 |  31:0 | **DATA3** - see `VF2GUC_MMIO_RELAY_SERVICE`_ response        |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_MMIO_RELAY_SUCCESS		0x5007

#define PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_LEN	(GUC_HXG_REQUEST_MSG_MIN_LEN + 5u)
#define PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_1_VFID	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_2_MAGIC	(0xf << 24)
#define PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_2_DATA0	(0xffffff << 0)
#define PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_n_DATAx	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_MMIO_RELAY_SUCCESS_REQUEST_MSG_NUM_DATA	3u

/**
 * DOC: PF2GUC_MMIO_RELAY_FAILURE
 *
 * The `PF2GUC_MMIO_RELAY_FAILURE`_ message is used by PF to send error response
 * data related to `VF2GUC_MMIO_RELAY_SERVICE`_ request message that
 * was received in `GUC2PF_MMIO_RELAY_SERVICE`_.
 *
 * This message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_FAST_REQUEST_                            |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_MMIO_RELAY_FAILURE` = 0x5008    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - identifier of the VF where to send reply          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 | 31:28 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:24 | **MAGIC** - see `VF2GUC_MMIO_RELAY_SERVICE`_ request         |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  23:8 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **FAULT** - see `IOV Error Codes`_                           |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_MMIO_RELAY_FAILURE		0x5008

#define PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_LEN	(GUC_HXG_REQUEST_MSG_MIN_LEN + 2u)
#define PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_1_VFID	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_2_MAGIC	(0xf << 24)
#define PF2GUC_MMIO_RELAY_FAILURE_REQUEST_MSG_2_FAULT	(0xff << 0)

/**
 * DOC: GUC2PF_ADVERSE_EVENT
 *
 * This message is used by the GuC to notify PF about adverse events.
 *
 * This G2H message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2PF_ADVERSE_EVENT` = 0x5104         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **THRESHOLD** - key of the exceeded threshold        |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2PF_ADVERSE_EVENT			0x5104

#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_2_THRESHOLD	GUC_HXG_EVENT_MSG_n_DATAn

/**
 * DOC: GUC2PF_VF_STATE_NOTIFY
 *
 * The GUC2PF_VF_STATE_NOTIFY message is used by the GuC to notify PF about change
 * of the VF state.
 *
 * This G2H message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2PF_VF_STATE_NOTIFY` = 0x5106       |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **EVENT** - notification event:                      |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_ENABLE` = 1 (only if VFID = 0)        |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_FLR` = 1                              |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_FLR_DONE` = 2                         |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_PAUSE_DONE` = 3                       |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_FIXUP_DONE` = 4                       |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2PF_VF_STATE_NOTIFY		0x5106

#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_2_EVENT	GUC_HXG_EVENT_MSG_n_DATAn
#define   GUC_PF_NOTIFY_VF_ENABLE			1
#define   GUC_PF_NOTIFY_VF_FLR				1
#define   GUC_PF_NOTIFY_VF_FLR_DONE			2
#define   GUC_PF_NOTIFY_VF_PAUSE_DONE			3
#define   GUC_PF_NOTIFY_VF_FIXUP_DONE			4

/**
 * DOC: PF2GUC_VF_CONTROL
 *
 * The PF2GUC_VF_CONTROL message is used by the PF to trigger VF state change
 * maintained by the GuC.
 *
 * This H2G message must be sent as `CTB HXG Message`_.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_VF_CONTROL_CMD` = 0x5506        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **COMMAND** - control command:                       |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_PAUSE` = 1                           |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_RESUME` = 2                          |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_STOP` = 3                            |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_FLR_START` = 4                       |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_FLR_FINISH` = 5                      |
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
#define GUC_ACTION_PF2GUC_VF_CONTROL			0x5506

#define PF2GUC_VF_CONTROL_REQUEST_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define PF2GUC_VF_CONTROL_REQUEST_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define PF2GUC_VF_CONTROL_REQUEST_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define PF2GUC_VF_CONTROL_REQUEST_MSG_2_COMMAND		GUC_HXG_EVENT_MSG_n_DATAn
#define   GUC_PF_TRIGGER_VF_PAUSE			1
#define   GUC_PF_TRIGGER_VF_RESUME			2
#define   GUC_PF_TRIGGER_VF_STOP			3
#define   GUC_PF_TRIGGER_VF_FLR_START			4
#define   GUC_PF_TRIGGER_VF_FLR_FINISH			5

/**
 * DOC: PF2GUC_SAVE_RESTORE_VF
 *
 * This message is used by the PF to migrate VF info state maintained by the GuC.
 *
 * This message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = **OPCODE** - operation to take:                      |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_PF_OPCODE_VF_SAVE` = 0                             |
 *  |   |       |   - _`GUC_PF_OPCODE_VF_RESTORE` = 1                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_SAVE_RESTORE_VF` = 0x550B       |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **BUFF_LO** - lower 32-bits of GGTT offset to the 4K |
 *  |   |       | buffer where the VF info will be save to or restored from.   |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | DATA3 = **BUFF_HI** - upper 32-bits of GGTT offset to the 4K |
 *  |   |       | buffer where the VF info will be save to or restored from.   |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = **USED** - size of buffer used (in bytes)            |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_SAVE_RESTORE_VF		0x550B

#define PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 3u)
#define PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_0_OPCODE	GUC_HXG_EVENT_MSG_0_DATA0
#define   GUC_PF_OPCODE_VF_SAVE				0
#define   GUC_PF_OPCODE_VF_RESTORE			1
#define PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_1_VFID	GUC_HXG_EVENT_MSG_n_DATAn
#define PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_2_BUFF_LO	GUC_HXG_EVENT_MSG_n_DATAn
#define PF2GUC_SAVE_RESTORE_VF_REQUEST_MSG_3_BUFF_HI	GUC_HXG_EVENT_MSG_n_DATAn

#define PF2GUC_SAVE_RESTORE_VF_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define PF2GUC_SAVE_RESTORE_VF_RESPONSE_MSG_0_USED	GUC_HXG_RESPONSE_MSG_0_DATA0

#endif /* __GUC_ACTIONS_PF_ABI_H__ */
