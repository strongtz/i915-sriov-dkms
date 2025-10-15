/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/hrtimer.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
static enum hrtimer_restart hrtimer_dummy_timeout(struct hrtimer *unused)
{
	return HRTIMER_NORESTART;
}

void hrtimer_setup(struct hrtimer *timer, enum hrtimer_restart (*function)(struct hrtimer *),
		   clockid_t clock_id, enum hrtimer_mode mode)
{
	hrtimer_init(timer, clock_id, mode);
	if (WARN_ON_ONCE(!function))
		timer->function = hrtimer_dummy_timeout;
	else
		timer->function = function;
}
EXPORT_SYMBOL(hrtimer_setup);
#endif
