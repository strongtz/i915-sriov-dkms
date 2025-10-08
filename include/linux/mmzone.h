#include_next <linux/mmzone.h>

#ifndef __BACKPORT_LINUX_MMZONE_H__
#define __BACKPORT_LINUX_MMZONE_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
static inline struct dev_pagemap *page_pgmap(const struct page *page)
{
	return page->pgmap;
}
#endif

#endif
