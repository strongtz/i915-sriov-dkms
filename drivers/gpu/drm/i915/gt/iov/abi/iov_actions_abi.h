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

/**
 * DOC: VF2PF_UPDATE_GGTT32
 *
 * This `IOV Message`_ is used to request the PF to update the GGTT mapping
 * using the PTE provided by the VF.
 * If more than one PTE should be mapped, then the next PTEs are generated by
 * the PF based on first or last PTE (depending on the MODE) or based onu
 * subsequent provided PTEs.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   |       | TYPE = GUC_HXG_TYPE_FAST_REQUEST_ (only if FLAGS = 0)        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`VF2PF_UPDATE_GGTT32` = 0x0102                     |
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
 *  | 4 |  31:0 | **PTE_LO** - lower 32 bits of GGTT PTE1                      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 5 |  31:0 | **PTE_HI** - upper 32 bits of GGTT PTE1                      |
 *  +---+-------+--------------------------------------------------------------+
 *  :   :       :                                                              :
 *  +---+-------+--------------------------------------------------------------+
 *  |n-1|  31:0 | **PTE_LO** - lower 32 bits of GGTT PTEn                      |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | **PTE_HI** - upper 32 bits of GGTT PTEn                      |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **NUM_PTES** - number of PTEs entries updated                |
 *  +---+-------+--------------------------------------------------------------+
 */

#define IOV_ACTION_VF2PF_UPDATE_GGTT32			0x102

#define VF2PF_UPDATE_GGTT32_REQUEST_MSG_MIN_LEN		2u
#define VF2PF_UPDATE_GGTT32_REQUEST_MSG_MAX_LEN		VF2GUC_RELAY_TO_PF_REQUEST_MSG_MAX_LEN
#define VF2PF_UPDATE_GGTT32_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_OFFSET	(0xfffff << 12)
#define VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_MODE		(0x3 << 10)
#define   VF2PF_UPDATE_GGTT32_MODE_DUPLICATE		0u
#define   VF2PF_UPDATE_GGTT32_MODE_REPLICATE		1u
#define   VF2PF_UPDATE_GGTT32_MODE_DUPLICATE_LAST	2u
#define   VF2PF_UPDATE_GGTT32_MODE_REPLICATE_LAST	3u
#define VF2PF_UPDATE_GGTT32_REQUEST_MSG_1_NUM_COPIES	(0x3ff << 0)
#define VF2PF_UPDATE_GGTT32_REQUEST_DATAn_PTE_LO	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2PF_UPDATE_GGTT32_REQUEST_DATAn_PTE_HI	GUC_HXG_REQUEST_MSG_n_DATAn
#define   VF2PF_UPDATE_GGTT_MAX_PTES \
	  ((VF2PF_MSG_MAX_LEN - VF2PF_UPDATE_GGTT32_REQUEST_MSG_MIN_LEN) / 2)

#define VF2PF_UPDATE_GGTT32_RESPONSE_MSG_LEN		1u
#define VF2PF_UPDATE_GGTT32_RESPONSE_MSG_0_NUM_PTES	GUC_HXG_RESPONSE_MSG_0_DATA0

#define VF2PF_UPDATE_GGTT32_IS_LAST_MODE(_mode) \
	((_mode) == VF2PF_UPDATE_GGTT32_MODE_DUPLICATE_LAST || \
	 (_mode) == VF2PF_UPDATE_GGTT32_MODE_REPLICATE_LAST)
#endif /* _ABI_IOV_ACTIONS_ABI_H_ */
