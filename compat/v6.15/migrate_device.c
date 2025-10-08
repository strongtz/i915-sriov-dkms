/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/migrate.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(6,15,0)
/**
 * migrate_device_pfns() - migrate device private pfns to normal memory.
 * @src_pfns: pre-popluated array of source device private pfns to migrate.
 * @npages: number of pages to migrate.
 *
 * Similar to migrate_device_range() but supports non-contiguous pre-popluated
 * array of device pages to migrate.
 */
int migrate_device_pfns(unsigned long *src_pfns, unsigned long npages)
{
	unsigned long i;

        //TODO: use kallsyms to avoid call migrate_device_unmap per page
	for (i = 0; i < npages; i++)
		migrate_device_range(&src_pfns[i], src_pfns[i], 1);

	return 0;
}
EXPORT_SYMBOL(migrate_device_pfns);
#endif
