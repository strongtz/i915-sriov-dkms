/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _ABI_IOV_ACTIONS_DEBUG_ABI_H_
#define _ABI_IOV_ACTIONS_DEBUG_ABI_H_

#include "iov_actions_abi.h"

/**
 * DOC: IOV debug actions
 *
 * These range of IOV action codes is reserved for debug and may only be
 * used on selected debug configs.
 *
 *  _`IOV_ACTION_DEBUG_ONLY_START` = 0xDEB0
 *  _`IOV_ACTION_DEBUG_ONLY_END` = 0xDEFF
 */

#define IOV_ACTION_DEBUG_ONLY_START	0xDEB0
#define IOV_ACTION_DEBUG_ONLY_END	0xDEFF

#endif /* _ABI_IOV_ACTIONS_DEBUG_ABI_H_ */
