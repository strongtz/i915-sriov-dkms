/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_print.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
/**
 * drm_dp_lttpr_set_transparent_mode() - set the LTTPR in transparent mode
 * @aux: DisplayPort AUX channel
 * @enable: Enable or disable transparent mode
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int drm_dp_lttpr_set_transparent_mode(struct drm_dp_aux *aux, bool enable)
{
	u8 val = enable ? DP_PHY_REPEATER_MODE_TRANSPARENT :
			  DP_PHY_REPEATER_MODE_NON_TRANSPARENT;
	int ret = drm_dp_dpcd_writeb(aux, DP_PHY_REPEATER_MODE, val);

	if (ret < 0)
		return ret;

	return (ret == 1) ? 0 : -EIO;
}
EXPORT_SYMBOL(drm_dp_lttpr_set_transparent_mode);

/**
 * drm_dp_lttpr_init() - init LTTPR transparency mode according to DP standard
 * @aux: DisplayPort AUX channel
 * @lttpr_count: Number of LTTPRs. Between 0 and 8, according to DP standard.
 *               Negative error code for any non-valid number.
 *               See drm_dp_lttpr_count().
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int drm_dp_lttpr_init(struct drm_dp_aux *aux, int lttpr_count)
{
	int ret;

	if (!lttpr_count)
		return 0;

	/*
	 * See DP Standard v2.0 3.6.6.1 about the explicit disabling of
	 * non-transparent mode and the disable->enable non-transparent mode
	 * sequence.
	 */
	ret = drm_dp_lttpr_set_transparent_mode(aux, true);
	if (ret)
		return ret;

	if (lttpr_count < 0)
		return -ENODEV;

	if (drm_dp_lttpr_set_transparent_mode(aux, false)) {
		/*
		 * Roll-back to transparent mode if setting non-transparent
		 * mode has failed
		 */
		drm_dp_lttpr_set_transparent_mode(aux, true);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_lttpr_init);

/**
 * drm_dp_lttpr_wake_timeout_setup() - Grant extended time for sink to wake up
 * @aux: The DP AUX channel to use
 * @transparent_mode: This is true if lttpr is in transparent mode
 *
 * This function checks if the sink needs any extended wake time, if it does
 * it grants this request. Post this setup the source device can keep trying
 * the Aux transaction till the granted wake timeout.
 * If this function is not called all Aux transactions are expected to take
 * a default of 1ms before they throw an error.
 */
void drm_dp_lttpr_wake_timeout_setup(struct drm_dp_aux *aux, bool transparent_mode)
{
	u8 val = 1;
	int ret;

	if (transparent_mode) {
		static const u8 timeout_mapping[] = {
			[DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_1_MS] = 1,
			[DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_20_MS] = 20,
			[DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_40_MS] = 40,
			[DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_60_MS] = 60,
			[DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_80_MS] = 80,
			[DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_100_MS] = 100,
		};

		ret = drm_dp_dpcd_readb(aux, DP_EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_REQUEST, &val);
		if (ret != 1) {
			drm_dbg_kms(aux->drm_dev,
				    "Failed to read Extended sleep wake timeout request\n");
			return;
		}

		val = (val < sizeof(timeout_mapping) && timeout_mapping[val]) ?
			timeout_mapping[val] : 1;

		if (val > 1)
			drm_dp_dpcd_writeb(aux,
					   DP_EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_GRANT,
					   DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_GRANTED);
	} else {
		ret = drm_dp_dpcd_readb(aux, DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT, &val);
		if (ret != 1) {
			drm_dbg_kms(aux->drm_dev,
				    "Failed to read Extended sleep wake timeout request\n");
			return;
		}

		val = (val & DP_EXTENDED_WAKE_TIMEOUT_REQUEST_MASK) ?
			(val & DP_EXTENDED_WAKE_TIMEOUT_REQUEST_MASK) * 10 : 1;

		if (val > 1)
			drm_dp_dpcd_writeb(aux, DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT,
					   DP_EXTENDED_WAKE_TIMEOUT_GRANT);
	}
}
EXPORT_SYMBOL(drm_dp_lttpr_wake_timeout_setup);
#endif
