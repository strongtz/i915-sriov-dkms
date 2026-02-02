#include_next <linux/sizes.h>

#ifndef __BACKPORT_LINUX_SIZES_H__
#define __BACKPORT_LINUX_SIZES_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
#define SZ_128T				_AC(0x800000000000, ULL)
#endif

#endif /* __BACKPORT_LINUX_SIZES_H__ */
