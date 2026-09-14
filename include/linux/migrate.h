#include_next <linux/migrate.h>
#ifndef __BACKPORT_LINUX_MIGRATE_H__
#define __BACKPORT_LINUX_MIGRATE_H__
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
#define MIGRATE_PFN_COMPOUND	(1UL << 4)
#define MIGRATE_VMA_SELECT_COMPOUND ((enum migrate_vma_direction)(1 << 4))
#endif
#endif