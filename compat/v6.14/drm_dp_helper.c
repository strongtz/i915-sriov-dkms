/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/iopoll.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_print.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,14,0)
static int read_payload_update_status(struct drm_dp_aux *aux)
{
	int ret;
	u8 status;

	ret = drm_dp_dpcd_read_byte(aux, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
	if (ret < 0)
		return ret;

	return status;
}

/**
 * drm_dp_dpcd_write_payload() - Write Virtual Channel information to payload table
 * @aux: DisplayPort AUX channel
 * @vcpid: Virtual Channel Payload ID
 * @start_time_slot: Starting time slot
 * @time_slot_count: Time slot count
 *
 * Write the Virtual Channel payload allocation table, checking the payload
 * update status and retrying as necessary.
 *
 * Returns:
 * 0 on success, negative error otherwise
 */
int drm_dp_dpcd_write_payload(struct drm_dp_aux *aux,
			      int vcpid, u8 start_time_slot, u8 time_slot_count)
{
	u8 payload_alloc[3], status;
	int ret;
	int retries = 0;

	drm_dp_dpcd_write_byte(aux, DP_PAYLOAD_TABLE_UPDATE_STATUS,
			       DP_PAYLOAD_TABLE_UPDATED);

	payload_alloc[0] = vcpid;
	payload_alloc[1] = start_time_slot;
	payload_alloc[2] = time_slot_count;

	ret = drm_dp_dpcd_write_data(aux, DP_PAYLOAD_ALLOCATE_SET, payload_alloc, 3);
	if (ret < 0) {
		drm_dbg_kms(aux->drm_dev, "failed to write payload allocation %d\n", ret);
		goto fail;
	}

retry:
	ret = drm_dp_dpcd_read_byte(aux, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
	if (ret < 0) {
		drm_dbg_kms(aux->drm_dev, "failed to read payload table status %d\n", ret);
		goto fail;
	}

	if (!(status & DP_PAYLOAD_TABLE_UPDATED)) {
		retries++;
		if (retries < 20) {
			usleep_range(10000, 20000);
			goto retry;
		}
		drm_dbg_kms(aux->drm_dev, "status not set after read payload table status %d\n",
			    status);
		ret = -EINVAL;
		goto fail;
	}
	ret = 0;
fail:
	return ret;
}
EXPORT_SYMBOL(drm_dp_dpcd_write_payload);

/**
 * drm_dp_dpcd_poll_act_handled() - Poll for ACT handled status
 * @aux: DisplayPort AUX channel
 * @timeout_ms: Timeout in ms
 *
 * Try waiting for the sink to finish updating its payload table by polling for
 * the ACT handled bit of DP_PAYLOAD_TABLE_UPDATE_STATUS for up to @timeout_ms
 * milliseconds, defaulting to 3000 ms if 0.
 *
 * Returns:
 * 0 if the ACT was handled in time, negative error code on failure.
 */
int drm_dp_dpcd_poll_act_handled(struct drm_dp_aux *aux, int timeout_ms)
{
	int ret, status;

	/* default to 3 seconds, this is arbitrary */
	timeout_ms = timeout_ms ?: 3000;

	ret = readx_poll_timeout(read_payload_update_status, aux, status,
				 status & DP_PAYLOAD_ACT_HANDLED || status < 0,
				 200, timeout_ms * USEC_PER_MSEC);
	if (ret < 0 && status >= 0) {
		drm_err(aux->drm_dev, "Failed to get ACT after %d ms, last status: %02x\n",
			timeout_ms, status);
		return -EINVAL;
	} else if (status < 0) {
		/*
		 * Failure here isn't unexpected - the hub may have
		 * just been unplugged
		 */
		drm_dbg_kms(aux->drm_dev, "Failed to read payload table status: %d\n", status);
		return status;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_dpcd_poll_act_handled);
#endif
