#include_next <linux/memremap.h>
#ifndef __BACKPORT_LINUX_MEMREMAP_H
#define __BACKPORT_LINUX_MEMREMAP_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
static inline void *folio_zone_device_data(const struct folio *folio)
{
	VM_WARN_ON_FOLIO(!folio_is_device_private(folio), folio);
	return folio->page.zone_device_data;
}

static inline void folio_set_zone_device_data(struct folio *folio, void *data)
{
	VM_WARN_ON_FOLIO(!folio_is_device_private(folio), folio);
	folio->page.zone_device_data = data;
}
#endif

#endif /* __BACKPORT_LINUX_MEMREMAP_H */