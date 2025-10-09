#include_next <drm/display/drm_dp_helper.h>
#ifndef __BACKPORT_DRM_DP_HELPER_H__
#define __BACKPORT_DRM_DP_HELPER_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 14, 0)
/**
 * drm_dp_dpcd_read_data() - read a series of bytes from the DPCD
 * @aux: DisplayPort AUX channel (SST or MST)
 * @offset: address of the (first) register to read
 * @buffer: buffer to store the register values
 * @size: number of bytes in @buffer
 *
 * Returns zero (0) on success, or a negative error
 * code on failure. -EIO is returned if the request was NAKed by the sink or
 * if the retry count was exceeded. If not all bytes were transferred, this
 * function returns -EPROTO. Errors from the underlying AUX channel transfer
 * function, with the exception of -EBUSY (which causes the transaction to
 * be retried), are propagated to the caller.
 */
static inline int drm_dp_dpcd_read_data(struct drm_dp_aux *aux,
					unsigned int offset,
					void *buffer, size_t size)
{
	int ret;

	ret = drm_dp_dpcd_read(aux, offset, buffer, size);
	if (ret < 0)
		return ret;
	if (ret < size)
		return -EPROTO;

	return 0;
}

/**
 * drm_dp_dpcd_write_data() - write a series of bytes to the DPCD
 * @aux: DisplayPort AUX channel (SST or MST)
 * @offset: address of the (first) register to write
 * @buffer: buffer containing the values to write
 * @size: number of bytes in @buffer
 *
 * Returns zero (0) on success, or a negative error
 * code on failure. -EIO is returned if the request was NAKed by the sink or
 * if the retry count was exceeded. If not all bytes were transferred, this
 * function returns -EPROTO. Errors from the underlying AUX channel transfer
 * function, with the exception of -EBUSY (which causes the transaction to
 * be retried), are propagated to the caller.
 */
static inline int drm_dp_dpcd_write_data(struct drm_dp_aux *aux,
					 unsigned int offset,
					 void *buffer, size_t size)
{
	int ret;

	ret = drm_dp_dpcd_write(aux, offset, buffer, size);
	if (ret < 0)
		return ret;
	if (ret < size)
		return -EPROTO;

	return 0;
}

/**
 * drm_dp_dpcd_read_byte() - read a single byte from the DPCD
 * @aux: DisplayPort AUX channel
 * @offset: address of the register to read
 * @valuep: location where the value of the register will be stored
 *
 * Returns zero (0) on success, or a negative error code on failure.
 */
static inline int drm_dp_dpcd_read_byte(struct drm_dp_aux *aux,
					unsigned int offset, u8 *valuep)
{
	return drm_dp_dpcd_read_data(aux, offset, valuep, 1);
}

/**
 * drm_dp_dpcd_write_byte() - write a single byte to the DPCD
 * @aux: DisplayPort AUX channel
 * @offset: address of the register to write
 * @value: value to write to the register
 *
 * Returns zero (0) on success, or a negative error code on failure.
 */
static inline int drm_dp_dpcd_write_byte(struct drm_dp_aux *aux,
					 unsigned int offset, u8 value)
{
	return drm_dp_dpcd_write_data(aux, offset, &value, 1);
}

#define drm_dp_dpcd_write_payload LINUX_BACKPORT(drm_dp_dpcd_write_payload)
int drm_dp_dpcd_write_payload(struct drm_dp_aux *aux,
			      int vcpid, u8 start_time_slot, u8 time_slot_count);

#define drm_dp_dpcd_poll_act_handled LINUX_BACKPORT(drm_dp_dpcd_poll_act_handled)
int drm_dp_dpcd_poll_act_handled(struct drm_dp_aux *aux, int timeout_ms);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
#define DP_EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_GRANT	    0x119   /* 1.4a */
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_GRANTED	    (1 << 0)

#define DP_EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_REQUEST	0x2211  /* 1.4a */
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_MASK		0xff
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_1_MS		0x00
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_20_MS	0x01
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_40_MS	0x02
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_60_MS	0x03
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_80_MS	0x04
# define DP_DPRX_SLEEP_WAKE_TIMEOUT_PERIOD_100_MS	0x05

# define DP_EXTENDED_WAKE_TIMEOUT_REQUEST_MASK		0x7f
# define DP_EXTENDED_WAKE_TIMEOUT_GRANT			(1 << 7)

#define drm_dp_lttpr_set_transparent_mode LINUX_BACKPORT(drm_dp_lttpr_set_transparent_mode)
int drm_dp_lttpr_set_transparent_mode(struct drm_dp_aux *aux, bool enable);
#define drm_dp_lttpr_init LINUX_BACKPORT(drm_dp_lttpr_init)
int drm_dp_lttpr_init(struct drm_dp_aux *aux, int lttpr_count);

#define drm_dp_lttpr_wake_timeout_setup LINUX_BACKPORT(drm_dp_lttpr_wake_timeout_setup)
void drm_dp_lttpr_wake_timeout_setup(struct drm_dp_aux *aux, bool transparent_mode);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
#define drm_dp_link_symbol_cycles LINUX_BACKPORT(drm_dp_link_symbol_cycles)
int drm_dp_link_symbol_cycles(int lane_count, int pixels, int dsc_slice_count,
			      int bpp_x16, int symbol_size, bool is_mst);
#endif

#endif /* __BACKPORT_DRM_DP_HELPER_H__ */
