#include_next <linux/iopoll.h>

#ifndef __BACKPORT_LINUX_IOPOLL_H__
#define __BACKPORT_LINUX_IOPOLL_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
/**
 * poll_timeout_us - Periodically poll and perform an operation until
 *                   a condition is met or a timeout occurs
 *
 * @op: Operation
 * @cond: Break condition
 * @sleep_us: Maximum time to sleep between operations in us (0 tight-loops).
 *            Please read usleep_range() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @sleep_before_op: if it is true, sleep @sleep_us before operation.
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 */
#define poll_timeout_us(op, cond, sleep_us, timeout_us, sleep_before_op) \
({ \
	u64 __timeout_us = (timeout_us); \
	unsigned long __sleep_us = (sleep_us); \
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us); \
	int ___ret; \
	might_sleep_if((__sleep_us) != 0); \
	if ((sleep_before_op) && __sleep_us) \
		usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
	for (;;) { \
		bool __expired = __timeout_us && \
			ktime_compare(ktime_get(), __timeout) > 0; \
		/* guarantee 'op' and 'cond' are evaluated after timeout expired */ \
		barrier(); \
		op; \
		if (cond) { \
			___ret = 0; \
			break; \
		} \
		if (__expired) { \
			___ret = -ETIMEDOUT; \
			break; \
		} \
		if (__sleep_us) \
			usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
		cpu_relax(); \
	} \
	___ret; \
})

/**
 * poll_timeout_us_atomic - Periodically poll and perform an operation until
 *                          a condition is met or a timeout occurs
 *
 * @op: Operation
 * @cond: Break condition
 * @delay_us: Time to udelay between operations in us (0 tight-loops).
 *            Please read udelay() function description for details and
 *            limitations.
 * @timeout_us: Timeout in us, 0 means never timeout
 * @delay_before_op: if it is true, delay @delay_us before operation.
 *
 * This macro does not rely on timekeeping.  Hence it is safe to call even when
 * timekeeping is suspended, at the expense of an underestimation of wall clock
 * time, which is rather minimal with a non-zero delay_us.
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 *
 * Returns: 0 on success and -ETIMEDOUT upon a timeout.
 */
#define poll_timeout_us_atomic(op, cond, delay_us, timeout_us, \
			       delay_before_op) \
({ \
	u64 __timeout_us = (timeout_us); \
	s64 __left_ns = __timeout_us * NSEC_PER_USEC; \
	unsigned long __delay_us = (delay_us); \
	u64 __delay_ns = __delay_us * NSEC_PER_USEC; \
	int ___ret; \
	if ((delay_before_op) && __delay_us) { \
		udelay(__delay_us); \
		if (__timeout_us) \
			__left_ns -= __delay_ns; \
	} \
	for (;;) { \
		bool __expired = __timeout_us && __left_ns < 0; \
		/* guarantee 'op' and 'cond' are evaluated after timeout expired */ \
		barrier(); \
		op; \
		if (cond) { \
			___ret = 0; \
			break; \
		} \
		if (__expired) { \
			___ret = -ETIMEDOUT; \
			break; \
		} \
		if (__delay_us) { \
			udelay(__delay_us); \
			if (__timeout_us) \
				__left_ns -= __delay_ns; \
		} \
		cpu_relax(); \
		if (__timeout_us) \
			__left_ns--; \
	} \
	___ret; \
})
#endif

#endif
