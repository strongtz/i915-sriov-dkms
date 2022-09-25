/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _ABI_IOV_ERRORS_ABI_H_
#define _ABI_IOV_ERRORS_ABI_H_

/**
 * DOC: IOV Error Codes
 *
 * IOV uses error codes that mostly match errno values.
 */

#define IOV_ERROR_UNDISCLOSED			0
#define IOV_ERROR_OPERATION_NOT_PERMITTED	1	/* EPERM */
#define IOV_ERROR_PERMISSION_DENIED		13	/* EACCES */
#define IOV_ERROR_INVALID_ARGUMENT		22	/* EINVAL */
#define IOV_ERROR_INVALID_REQUEST_CODE		56	/* EBADRQC */
#define IOV_ERROR_NO_DATA_AVAILABLE		61	/* ENODATA */
#define IOV_ERROR_PROTOCOL_ERROR		71	/* EPROTO */
#define IOV_ERROR_MESSAGE_SIZE			90	/* EMSGSIZE */

#endif /* _ABI_IOV_ERRORS_ABI_H_ */
