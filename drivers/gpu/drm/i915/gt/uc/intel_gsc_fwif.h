/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _INTEL_GSC_FWIF_H_
#define _INTEL_GSC_FWIF_H_

#include <linux/types.h>

struct intel_gsc_mtl_header {
	u32 validity_marker;
#define GSC_HECI_VALIDITY_MARKER 0xA578875A

	u8 gsc_address;
#define HECI_MEADDRESS_PROXY 10
#define HECI_MEADDRESS_PXP 17
#define HECI_MEADDRESS_HDCP 18

	u8 reserved1;

	u16 header_version;
#define MTL_GSC_HEADER_VERSION 1

	u64 host_session_handle;
	u64 gsc_message_handle;

	u32 message_size; /* lower 20 bits only, upper 12 are reserved */

	/*
	 * Flags mask:
	 * Bit 0: Pending
	 * Bit 1: Session Cleanup;
	 * Bits 2-15: Flags
	 * Bits 16-31: Extension Size
	 */
	u32 flags;

	u32 status;
} __packed;

struct intel_gsc_proxy_header {
	/*
	 * hdr:
	 * Bits 0-7: type of the proxy message (see enum intel_gsc_proxy_type)
	 * Bits 8-15: rsvd
	 * Bits 16-31: length in bytes of the payload following the proxy header
	 */
	u32 hdr;
#define GSC_PROXY_TYPE		 GENMASK(7, 0)
#define GSC_PROXY_PAYLOAD_LENGTH GENMASK(31, 16)

	u32 source;		/* Source of the Proxy message */
	u32 destination;	/* Destination of the Proxy message */
#define GSC_PROXY_ADDRESSING_KMD  0x10000
#define GSC_PROXY_ADDRESSING_GSC  0x20000
#define GSC_PROXY_ADDRESSING_CSME 0x30000

	u32 status;		/* Command status */
} __packed;

enum intel_gsc_proxy_type {
	GSC_PROXY_MSG_TYPE_PROXY_INVALID = 0,
	GSC_PROXY_MSG_TYPE_PROXY_QUERY = 1,
	GSC_PROXY_MSG_TYPE_PROXY_PAYLOAD = 2,
	GSC_PROXY_MSG_TYPE_PROXY_END = 3,
	GSC_PROXY_MSG_TYPE_PROXY_NOTIFICATION = 4,
};

#endif
