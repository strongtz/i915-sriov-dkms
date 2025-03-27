/*
 * Copyright (c) 2024
 *
 * Backport functionality introduced in Linux 6.10.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/bitfield.h>
#include <linux/byteorder/generic.h>
#include <linux/seq_buf.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_client.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
void drm_dp_as_sdp_log(struct drm_printer *p,
                       const struct drm_dp_as_sdp *as_sdp) {
  drm_printf(p, "DP SDP: AS_SDP, revision %u, length %u\n", as_sdp->revision,
             as_sdp->length);
  drm_printf(p, "    vtotal: %d\n", as_sdp->vtotal);
  drm_printf(p, "    target_rr: %d\n", as_sdp->target_rr);
  drm_printf(p, "    duration_incr_ms: %d\n", as_sdp->duration_incr_ms);
  drm_printf(p, "    duration_decr_ms: %d\n", as_sdp->duration_decr_ms);
  drm_printf(p, "    operation_mode: %d\n", as_sdp->mode);
}

/**
 * drm_dp_read_mst_cap() - Read the sink's MST mode capability
 * @aux: The DP AUX channel to use
 * @dpcd: A cached copy of the DPCD capabilities for this sink
 *
 * Returns: enum drm_dp_mst_mode to indicate MST mode capability
 */
enum drm_dp_mst_mode drm_dp_read_mst_cap_compat(
    struct drm_dp_aux *aux,
    const u8 dpcd[DP_RECEIVER_CAP_SIZE]) // BACKPORT_COMPAT
{
  u8 mstm_cap;

  if (dpcd[DP_DPCD_REV] < DP_DPCD_REV_12)
    return DRM_DP_SST;

  if (drm_dp_dpcd_readb(aux, DP_MSTM_CAP, &mstm_cap) != 1)
    return DRM_DP_SST;
  if (mstm_cap & DP_MST_CAP)
    return DRM_DP_MST;
  if (mstm_cap & DP_SINGLE_STREAM_SIDEBAND_MSG)
    return DRM_DP_SST_SIDEBAND_MSG;

  return DRM_DP_SST;
}


struct drm_edid {
	/* Size allocated for edid */
	size_t size;
	const struct edid *edid;
};
/**
 * drm_edid_get_product_id - Get the vendor and product identification
 * @drm_edid: EDID
 * @id: Where to place the product id
 */
void drm_edid_get_product_id(const struct drm_edid *drm_edid,
                             struct drm_edid_product_id *id) {
  if (drm_edid && drm_edid->edid && drm_edid->size >= EDID_LENGTH) {
    memcpy(&id->manufacturer_name, &drm_edid->edid->mfg_id,
           sizeof(id->manufacturer_name));
    memcpy(&id->product_code, &drm_edid->edid->prod_code,
           sizeof(id->product_code));
    memcpy(&id->serial_number, &drm_edid->edid->serial,
           sizeof(id->serial_number));
    memcpy(&id->week_of_manufacture, &drm_edid->edid->mfg_week,
           sizeof(id->week_of_manufacture));
    memcpy(&id->year_of_manufacture, &drm_edid->edid->mfg_year,
           sizeof(id->year_of_manufacture));
  } else {
    memset(id, 0, sizeof(*id));
  }
}

static void decode_date(struct seq_buf *s, const struct drm_edid_product_id *id)
{
	int week = id->week_of_manufacture;
	int year = id->year_of_manufacture + 1990;
	if (week == 0xff)
		seq_buf_printf(s, "model year: %d", year);
	else if (!week)
		seq_buf_printf(s, "year of manufacture: %d", year);
	else
		seq_buf_printf(s, "week/year of manufacture: %d/%d", week, year);
}
/**
 * drm_edid_print_product_id - Print decoded product id to printer
 * @p: drm printer
 * @id: EDID product id
 * @raw: If true, also print the raw hex
 *
 * See VESA E-EDID 1.4 section 3.4.
 */
void drm_edid_print_product_id(struct drm_printer *p,
			       const struct drm_edid_product_id *id, bool raw)
{
	DECLARE_SEQ_BUF(date, 40);
	char vend[4];
	drm_edid_decode_mfg_id(be16_to_cpu(id->manufacturer_name), vend);
	decode_date(&date, id);
	drm_printf(p, "manufacturer name: %s, product code: %u, serial number: %u, %s\n",
		   vend, le16_to_cpu(id->product_code),
		   le32_to_cpu(id->serial_number), seq_buf_str(&date));
	if (raw)
		drm_printf(p, "raw product id: %*ph\n", (int)sizeof(*id), id);
	WARN_ON(seq_buf_has_overflowed(&date));
}

static struct drm_vblank_crtc *
drm_vblank_crtc(struct drm_device *dev, unsigned int pipe)
{
	return &dev->vblank[pipe];
}

struct drm_vblank_crtc *
drm_crtc_vblank_crtc(struct drm_crtc *crtc)
{
	return drm_vblank_crtc(crtc->dev, drm_crtc_index(crtc));
}

/**
 * drm_dp_as_sdp_supported() - check if adaptive sync sdp is supported
 * @aux: DisplayPort AUX channel
 * @dpcd: DisplayPort configuration data
 *
 * Returns true if adaptive sync sdp is supported, else returns false
 */
bool drm_dp_as_sdp_supported(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 rx_feature;
	if (dpcd[DP_DPCD_REV] < DP_DPCD_REV_13)
		return false;
	if (drm_dp_dpcd_readb(aux, DP_DPRX_FEATURE_ENUMERATION_LIST_CONT_1,
			      &rx_feature) != 1) {
		drm_dbg_dp(aux->drm_dev,
			   "Failed to read DP_DPRX_FEATURE_ENUMERATION_LIST_CONT_1\n");
		return false;
	}
	return (rx_feature & DP_ADAPTIVE_SYNC_SDP_SUPPORTED);
}

/**
 * drm_dp_mst_aux_for_parent() - Get the AUX device for an MST port's parent
 * @port: MST port whose parent's AUX device is returned
 *
 * Return the AUX device for @port's parent or NULL if port's parent is the
 * root port.
 */
struct drm_dp_aux *drm_dp_mst_aux_for_parent(struct drm_dp_mst_port *port)
{
	if (!port->parent || !port->parent->port_parent)
		return NULL;
	return &port->parent->port_parent->aux;
}

void drm_client_dev_unregister(struct drm_device *dev)
{
	struct drm_client_dev *client, *tmp;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry_safe(client, tmp, &dev->clientlist, list) {
		list_del(&client->list);
		if (client->funcs && client->funcs->unregister) {
			client->funcs->unregister(client);
		} else {
			drm_client_release(client);
			kfree(client);
		}
	}
	mutex_unlock(&dev->clientlist_mutex);
}

/**
 *	sysfs_bin_attr_simple_read - read callback to simply copy from memory.
 *	@file:	attribute file which is being read.
 *	@kobj:	object to which the attribute belongs.
 *	@attr:	attribute descriptor.
 *	@buf:	destination buffer.
 *	@off:	offset in bytes from which to read.
 *	@count:	maximum number of bytes to read.
 *
 * Simple ->read() callback for bin_attributes backed by a buffer in memory.
 * The @private and @size members in struct bin_attribute must be set to the
 * buffer's location and size before the bin_attribute is created in sysfs.
 *
 * Bounds check for @off and @count is done in sysfs_kf_bin_read().
 * Negative value check for @off is done in vfs_setpos() and default_llseek().
 *
 * Returns number of bytes written to @buf.
 */
 ssize_t sysfs_bin_attr_simple_read(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf,
	loff_t off, size_t count)
{
	memcpy(buf, attr->private + off, count);
	return count;
}
EXPORT_SYMBOL_NS(sysfs_bin_attr_simple_read, I915_SRIOV_COMPAT);
#endif