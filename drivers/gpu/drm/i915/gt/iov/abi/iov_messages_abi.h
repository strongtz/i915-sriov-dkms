/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _ABI_IOV_MESSAGES_ABI_H_
#define _ABI_IOV_MESSAGES_ABI_H_

#include "gt/uc/abi/guc_actions_pf_abi.h"
#include "gt/uc/abi/guc_actions_vf_abi.h"
#include "gt/uc/abi/guc_messages_abi.h"

#define VF2PF_MSG_MAX_LEN \
	(GUC_CTB_MAX_DWORDS - PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN)

/**
 * DOC: IOV Message
 *
 * `IOV Message`_ is used in `IOV Communication`_.
 * Format of the `IOV Message`_ follows format of the generic `HXG Message`_.
 *
 *  +--------------------------------------------------------------------------+
 *  |  `IOV Message`_                                                          |
 *  +==========================================================================+
 *  |  `HXG Message`_                                                          |
 *  +--------------------------------------------------------------------------+
 *
 * In particular format of the _`IOV Request` is same as the `HXG Request`_.
 * Supported actions codes are listed in `IOV Actions`_.
 *
 * Format of the _`IOV Failure` is same as `HXG Failure`_.
 * See `IOV Error Codes`_ for possible error codes.
 */

static_assert(PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN >
	      VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN);

#endif /* _ABI_IOV_MESSAGES_ABI_H_ */
