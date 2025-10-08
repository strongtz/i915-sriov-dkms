#include_next <linux/migrate.h>

#ifndef __BACKPORT_LINUX_MIGRATE_H__
#define __BACKPORT_LINUX_MIGRATE_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,15,0)
int migrate_device_pfns(unsigned long *src_pfns, unsigned long npages);
#endif

#endif
