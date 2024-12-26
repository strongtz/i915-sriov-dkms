/*
 * Copyright (c) 2024
 *
 * Backport functionality introduced in Linux 6.9.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/string.h>

#include <drm/drm_print.h>
#include <drm/display/drm_dp_helper.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
/**
 * kmemdup_array - duplicate a given array.
 *
 * @src: array to duplicate.
 * @element_size: size of each element of array.
 * @count: number of elements to duplicate from array.
 * @gfp: GFP mask to use.
 *
 * Return: duplicated array of @src or %NULL in case of error,
 * result is physically contiguous. Use kfree() to free.
 */
void *kmemdup_array(const void *src, size_t element_size, size_t count, gfp_t gfp)
{
	return kmemdup(src, size_mul(element_size, count), gfp);
}

static const char *dp_pixelformat_get_name(enum dp_pixelformat pixelformat)
{
	if (pixelformat < 0 || pixelformat > DP_PIXELFORMAT_RESERVED)
		return "Invalid";

	switch (pixelformat) {
	case DP_PIXELFORMAT_RGB:
		return "RGB";
	case DP_PIXELFORMAT_YUV444:
		return "YUV444";
	case DP_PIXELFORMAT_YUV422:
		return "YUV422";
	case DP_PIXELFORMAT_YUV420:
		return "YUV420";
	case DP_PIXELFORMAT_Y_ONLY:
		return "Y_ONLY";
	case DP_PIXELFORMAT_RAW:
		return "RAW";
	default:
		return "Reserved";
	}
}

static const char *dp_colorimetry_get_name(enum dp_pixelformat pixelformat,
					   enum dp_colorimetry colorimetry)
{
	if (pixelformat < 0 || pixelformat > DP_PIXELFORMAT_RESERVED)
		return "Invalid";

	switch (colorimetry) {
	case DP_COLORIMETRY_DEFAULT:
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "sRGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.601";
		case DP_PIXELFORMAT_Y_ONLY:
			return "DICOM PS3.14";
		case DP_PIXELFORMAT_RAW:
			return "Custom Color Profile";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_WIDE_FIXED: /* and DP_COLORIMETRY_BT709_YCC */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Wide Fixed";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.709";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_WIDE_FLOAT: /* and DP_COLORIMETRY_XVYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Wide Float";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "xvYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_OPRGB: /* and DP_COLORIMETRY_XVYCC_709 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "OpRGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "xvYCC 709";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_DCI_P3_RGB: /* and DP_COLORIMETRY_SYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "DCI-P3";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "sYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_CUSTOM: /* and DP_COLORIMETRY_OPYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Custom Profile";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "OpYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_BT2020_RGB: /* and DP_COLORIMETRY_BT2020_CYCC */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "BT.2020 RGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.2020 CYCC";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_BT2020_YCC:
		switch (pixelformat) {
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.2020 YCC";
		default:
			return "Reserved";
		}
	default:
		return "Invalid";
	}
}

static const char *dp_dynamic_range_get_name(enum dp_dynamic_range dynamic_range)
{
	switch (dynamic_range) {
	case DP_DYNAMIC_RANGE_VESA:
		return "VESA range";
	case DP_DYNAMIC_RANGE_CTA:
		return "CTA range";
	default:
		return "Invalid";
	}
}

static const char *dp_content_type_get_name(enum dp_content_type content_type)
{
	switch (content_type) {
	case DP_CONTENT_TYPE_NOT_DEFINED:
		return "Not defined";
	case DP_CONTENT_TYPE_GRAPHICS:
		return "Graphics";
	case DP_CONTENT_TYPE_PHOTO:
		return "Photo";
	case DP_CONTENT_TYPE_VIDEO:
		return "Video";
	case DP_CONTENT_TYPE_GAME:
		return "Game";
	default:
		return "Reserved";
	}
}

void drm_dp_vsc_sdp_log_compat(struct drm_printer *p, const struct drm_dp_vsc_sdp *vsc) // BACKPORT_COMPAT
{
	drm_printf(p, "DP SDP: VSC, revision %u, length %u\n",
		   vsc->revision, vsc->length);
	drm_printf(p, "    pixelformat: %s\n",
		   dp_pixelformat_get_name(vsc->pixelformat));
	drm_printf(p, "    colorimetry: %s\n",
		   dp_colorimetry_get_name(vsc->pixelformat, vsc->colorimetry));
	drm_printf(p, "    bpc: %u\n", vsc->bpc);
	drm_printf(p, "    dynamic range: %s\n",
		   dp_dynamic_range_get_name(vsc->dynamic_range));
	drm_printf(p, "    content type: %s\n",
		   dp_content_type_get_name(vsc->content_type));
}

/**
 * drm_dp_max_dprx_data_rate - Get the max data bandwidth of a DPRX sink
 * @max_link_rate: max DPRX link rate in 10kbps units
 * @max_lanes: max DPRX lane count
 *
 * Given a link rate and lanes, get the data bandwidth.
 *
 * Data bandwidth is the actual payload rate, which depends on the data
 * bandwidth efficiency and the link rate.
 *
 * Note that protocol layers above the DPRX link level considered here can
 * further limit the maximum data rate. Such layers are the MST topology (with
 * limits on the link between the source and first branch device as well as on
 * the whole MST path until the DPRX link) and (Thunderbolt) DP tunnels -
 * which in turn can encapsulate an MST link with its own limit - with each
 * SST or MST encapsulated tunnel sharing the BW of a tunnel group.
 *
 * Returns the maximum data rate in kBps units.
 */
int drm_dp_max_dprx_data_rate(int max_link_rate, int max_lanes)
{
	int ch_coding_efficiency =
		drm_dp_bw_channel_coding_efficiency(drm_dp_is_uhbr_rate(max_link_rate));

	return DIV_ROUND_DOWN_ULL(mul_u32_u32(max_link_rate * 10 * max_lanes,
					      ch_coding_efficiency),
				  1000000 * 8);
}

/**
 * drm_dp_vsc_sdp_pack() - pack a given vsc sdp into generic dp_sdp
 * @vsc: vsc sdp initialized according to its purpose as defined in
 *       table 2-118 - table 2-120 in DP 1.4a specification
 * @sdp: valid handle to the generic dp_sdp which will be packed
 * @size: valid size of the passed sdp handle
 *
 * Returns length of sdp on success and error code on failure
 */
ssize_t drm_dp_vsc_sdp_pack(const struct drm_dp_vsc_sdp *vsc,
			    struct dp_sdp *sdp)
{
	size_t length = sizeof(struct dp_sdp);
	
	memset(sdp, 0, sizeof(struct dp_sdp));
	/*
	 * Prepare VSC Header for SU as per DP 1.4a spec, Table 2-119
	 * VSC SDP Header Bytes
	 */
	sdp->sdp_header.HB0 = 0; /* Secondary-Data Packet ID = 0 */
	sdp->sdp_header.HB1 = vsc->sdp_type; /* Secondary-data Packet Type */
	sdp->sdp_header.HB2 = vsc->revision; /* Revision Number */
	sdp->sdp_header.HB3 = vsc->length; /* Number of Valid Data Bytes */
	if (vsc->revision == 0x6) {
		sdp->db[0] = 1;
		sdp->db[3] = 1;
	}
	/*
	 * Revision 0x5 and revision 0x7 supports Pixel Encoding/Colorimetry
	 * Format as per DP 1.4a spec and DP 2.0 respectively.
	 */
	if (!(vsc->revision == 0x5 || vsc->revision == 0x7))
		goto out;
	/* VSC SDP Payload for DB16 through DB18 */
	/* Pixel Encoding and Colorimetry Formats  */
	sdp->db[16] = (vsc->pixelformat & 0xf) << 4; /* DB16[7:4] */
	sdp->db[16] |= vsc->colorimetry & 0xf; /* DB16[3:0] */
	switch (vsc->bpc) {
	case 6:
		/* 6bpc: 0x0 */
		break;
	case 8:
		sdp->db[17] = 0x1; /* DB17[3:0] */
		break;
	case 10:
		sdp->db[17] = 0x2;
		break;
	case 12:
		sdp->db[17] = 0x3;
		break;
	case 16:
		sdp->db[17] = 0x4;
		break;
	default:
		WARN(1, "Missing case %d\n", vsc->bpc);
		return -EINVAL;
	}
	/* Dynamic Range and Component Bit Depth */
	if (vsc->dynamic_range == DP_DYNAMIC_RANGE_CTA)
		sdp->db[17] |= 0x80;  /* DB17[7] */
	/* Content Type */
	sdp->db[18] = vsc->content_type & 0x7;
out:
	return length;
}
#endif