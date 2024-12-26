#include <linux/version.h>
#include_next <drm/drm_edid.h>

#ifndef _BACKPORT_DRM_DRM_EDID_H
#define _BACKPORT_DRM_DRM_EDID_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
struct drm_edid_product_id {
	__be16 manufacturer_name;
	__le16 product_code;
	__le32 serial_number;
	u8 week_of_manufacture;
	u8 year_of_manufacture;
} __packed;

void drm_edid_get_product_id(const struct drm_edid *drm_edid,
			     struct drm_edid_product_id *id);

struct drm_printer;

void drm_edid_print_product_id(struct drm_printer *p,
			       const struct drm_edid_product_id *id, bool raw);
#endif

#endif