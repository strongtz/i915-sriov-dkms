#include <linux/version.h>
#include_next <linux/backlight.h>

#ifndef _BACKPORT_LINUX_BACKLIGHT_H
#define _BACKPORT_LINUX_BACKLIGHT_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
#define BACKLIGHT_POWER_ON		(0)
#define BACKLIGHT_POWER_OFF		(4)
#define BACKLIGHT_POWER_REDUCED		(1) // deprecated; don't use in new code
#endif

#endif