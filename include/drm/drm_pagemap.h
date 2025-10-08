/* SPDX-License-Identifier: MIT */
#ifndef _DRM_PAGEMAP_H_
#define _DRM_PAGEMAP_H_

#include <linux/dma-direction.h>
#include <linux/hmm.h>
#include <linux/types.h>

#include "backport.h"

struct drm_pagemap;
struct drm_pagemap_zdd;
struct device;

/**
 * enum drm_interconnect_protocol - Used to identify an interconnect protocol.
 *
 * @DRM_INTERCONNECT_SYSTEM: DMA map is system pages
 * @DRM_INTERCONNECT_DRIVER: DMA map is driver defined
 */
enum drm_interconnect_protocol {
	DRM_INTERCONNECT_SYSTEM,
	DRM_INTERCONNECT_DRIVER,
	/* A driver can add private values beyond DRM_INTERCONNECT_DRIVER */
};

/**
 * struct drm_pagemap_device_addr - Device address representation.
 * @addr: The dma address or driver-defined address for driver private interconnects.
 * @proto: The interconnect protocol.
 * @order: The page order of the device mapping. (Size is PAGE_SIZE << order).
 * @dir: The DMA direction.
 *
 * Note: There is room for improvement here. We should be able to pack into
 * 64 bits.
 */
struct drm_pagemap_device_addr {
	dma_addr_t addr;
	u64 proto : 54;
	u64 order : 8;
	u64 dir : 2;
};

/**
 * drm_pagemap_device_addr_encode() - Encode a dma address with metadata
 * @addr: The dma address or driver-defined address for driver private interconnects.
 * @proto: The interconnect protocol.
 * @order: The page order of the dma mapping. (Size is PAGE_SIZE << order).
 * @dir: The DMA direction.
 *
 * Return: A struct drm_pagemap_device_addr encoding the above information.
 */
#define drm_pagemap_device_addr_encode LINUX_BACKPORT(drm_pagemap_device_addr_encode)
static inline struct drm_pagemap_device_addr
drm_pagemap_device_addr_encode(dma_addr_t addr,
			       enum drm_interconnect_protocol proto,
			       unsigned int order,
			       enum dma_data_direction dir)
{
	return (struct drm_pagemap_device_addr) {
		.addr = addr,
		.proto = proto,
		.order = order,
		.dir = dir,
	};
}

/**
 * struct drm_pagemap_ops: Ops for a drm-pagemap.
 */
struct drm_pagemap_ops {
	/**
	 * @device_map: Map for device access or provide a virtual address suitable for
	 *
	 * @dpagemap: The struct drm_pagemap for the page.
	 * @dev: The device mapper.
	 * @page: The page to map.
	 * @order: The page order of the device mapping. (Size is PAGE_SIZE << order).
	 * @dir: The transfer direction.
	 */
	struct drm_pagemap_device_addr (*device_map)(struct drm_pagemap *dpagemap,
						     struct device *dev,
						     struct page *page,
						     unsigned int order,
						     enum dma_data_direction dir);

	/**
	 * @device_unmap: Unmap a device address previously obtained using @device_map.
	 *
	 * @dpagemap: The struct drm_pagemap for the mapping.
	 * @dev: The device unmapper.
	 * @addr: The device address obtained when mapping.
	 */
	void (*device_unmap)(struct drm_pagemap *dpagemap,
			     struct device *dev,
			     struct drm_pagemap_device_addr addr);

	/**
	 * @populate_mm: Populate part of the mm with @dpagemap memory,
	 * migrating existing data.
	 * @dpagemap: The struct drm_pagemap managing the memory.
	 * @start: The virtual start address in @mm
	 * @end: The virtual end address in @mm
	 * @mm: Pointer to a live mm. The caller must have an mmget()
	 * reference.
	 *
	 * The caller will have the mm lock at least in read mode.
	 * Note that there is no guarantee that the memory is resident
	 * after the function returns, it's best effort only.
	 * When the mm is not using the memory anymore,
	 * it will be released. The struct drm_pagemap might have a
	 * mechanism in place to reclaim the memory and the data will
	 * then be migrated. Typically to system memory.
	 * The implementation should hold sufficient runtime power-
	 * references while pages are used in an address space and
	 * should ideally guard against hardware device unbind in
	 * a way such that device pages are migrated back to system
	 * followed by device page removal. The implementation should
	 * return -ENODEV after device removal.
	 *
	 * Return: 0 if successful. Negative error code on error.
	 */
	int (*populate_mm)(struct drm_pagemap *dpagemap,
			   unsigned long start, unsigned long end,
			   struct mm_struct *mm,
			   unsigned long timeslice_ms);
};

/**
 * struct drm_pagemap: Additional information for a struct dev_pagemap
 * used for device p2p handshaking.
 * @ops: The struct drm_pagemap_ops.
 * @dev: The struct drevice owning the device-private memory.
 */
struct drm_pagemap {
	const struct drm_pagemap_ops *ops;
	struct device *dev;
};

struct drm_pagemap_devmem;

/**
 * struct drm_pagemap_devmem_ops - Operations structure for GPU SVM device memory
 *
 * This structure defines the operations for GPU Shared Virtual Memory (SVM)
 * device memory. These operations are provided by the GPU driver to manage device memory
 * allocations and perform operations such as migration between device memory and system
 * RAM.
 */
struct drm_pagemap_devmem_ops {
	/**
	 * @devmem_release: Release device memory allocation (optional)
	 * @devmem_allocation: device memory allocation
	 *
	 * Release device memory allocation and drop a reference to device
	 * memory allocation.
	 */
	void (*devmem_release)(struct drm_pagemap_devmem *devmem_allocation);

	/**
	 * @populate_devmem_pfn: Populate device memory PFN (required for migration)
	 * @devmem_allocation: device memory allocation
	 * @npages: Number of pages to populate
	 * @pfn: Array of page frame numbers to populate
	 *
	 * Populate device memory page frame numbers (PFN).
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*populate_devmem_pfn)(struct drm_pagemap_devmem *devmem_allocation,
				   unsigned long npages, unsigned long *pfn);

	/**
	 * @copy_to_devmem: Copy to device memory (required for migration)
	 * @pages: Pointer to array of device memory pages (destination)
	 * @dma_addr: Pointer to array of DMA addresses (source)
	 * @npages: Number of pages to copy
	 *
	 * Copy pages to device memory.
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*copy_to_devmem)(struct page **pages,
			      dma_addr_t *dma_addr,
			      unsigned long npages);

	/**
	 * @copy_to_ram: Copy to system RAM (required for migration)
	 * @pages: Pointer to array of device memory pages (source)
	 * @dma_addr: Pointer to array of DMA addresses (destination)
	 * @npages: Number of pages to copy
	 *
	 * Copy pages to system RAM.
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*copy_to_ram)(struct page **pages,
			   dma_addr_t *dma_addr,
			   unsigned long npages);
};

/**
 * struct drm_pagemap_devmem - Structure representing a GPU SVM device memory allocation
 *
 * @dev: Pointer to the device structure which device memory allocation belongs to
 * @mm: Pointer to the mm_struct for the address space
 * @detached: device memory allocations is detached from device pages
 * @ops: Pointer to the operations structure for GPU SVM device memory
 * @dpagemap: The struct drm_pagemap of the pages this allocation belongs to.
 * @size: Size of device memory allocation
 * @timeslice_expiration: Timeslice expiration in jiffies
 */
struct drm_pagemap_devmem {
	struct device *dev;
	struct mm_struct *mm;
	struct completion detached;
	const struct drm_pagemap_devmem_ops *ops;
	struct drm_pagemap *dpagemap;
	size_t size;
	u64 timeslice_expiration;
};

#define drm_pagemap_migrate_to_devmem LINUX_BACKPORT(drm_pagemap_migrate_to_devmem)
int drm_pagemap_migrate_to_devmem(struct drm_pagemap_devmem *devmem_allocation,
				  struct mm_struct *mm,
				  unsigned long start, unsigned long end,
				  unsigned long timeslice_ms,
				  void *pgmap_owner);

#define drm_pagemap_evict_to_ram LINUX_BACKPORT(drm_pagemap_evict_to_ram)
int drm_pagemap_evict_to_ram(struct drm_pagemap_devmem *devmem_allocation);

#define drm_pagemap_pagemap_ops_get LINUX_BACKPORT(drm_pagemap_pagemap_ops_get)
const struct dev_pagemap_ops *drm_pagemap_pagemap_ops_get(void);

#define drm_pagemap_page_to_dpagemap LINUX_BACKPORT(drm_pagemap_page_to_dpagemap)
struct drm_pagemap *drm_pagemap_page_to_dpagemap(struct page *page);

#define drm_pagemap_devmem_init LINUX_BACKPORT(drm_pagemap_devmem_init)
void drm_pagemap_devmem_init(struct drm_pagemap_devmem *devmem_allocation,
			     struct device *dev, struct mm_struct *mm,
			     const struct drm_pagemap_devmem_ops *ops,
			     struct drm_pagemap *dpagemap, size_t size);

#define drm_pagemap_migrate_to_ram LINUX_BACKPORT(drm_pagemap_migrate_to_ram)
int drm_pagemap_populate_mm(struct drm_pagemap *dpagemap,
			    unsigned long start, unsigned long end,
			    struct mm_struct *mm,
			    unsigned long timeslice_ms);

#endif
