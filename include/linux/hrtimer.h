#include_next <linux/hrtimer.h>

#ifndef __BACKPORT_LINUX_HRTIMER_H__
#define __BACKPORT_LINUX_HRTIMER_H__
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
#define hrtimer_setup LINUX_BACKPORT(hrtimer_setup)
extern void hrtimer_setup(struct hrtimer *timer, enum hrtimer_restart (*function)(struct hrtimer *),
			  clockid_t clock_id, enum hrtimer_mode mode);
#endif
#endif
