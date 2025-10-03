// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/pagevec.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>
#include <linux/uio.h>

#include <drm/drm_cache.h>

#include "gem/i915_gem_region.h"
#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_gem_tiling.h"
#include "i915_gemfs.h"
#include "i915_scatterlist.h"
#include "i915_trace.h"

/*
 * Move folios to appropriate lru and release the batch, decrementing the
 * ref count of those folios.
 */
static void check_release_folio_batch(struct folio_batch *fbatch)
{
	check_move_unevictable_folios(fbatch);
	__folio_batch_release(fbatch);
	cond_resched();
}

void shmem_sg_free_table(struct sg_table *st, struct address_space *mapping,
			 bool dirty, bool backup)
{
	struct sgt_iter sgt_iter;
	struct folio_batch fbatch;
	struct folio *last = NULL;
	struct page *page;

	mapping_clear_unevictable(mapping);

	folio_batch_init(&fbatch);
	for_each_sgt_page(page, sgt_iter, st) {
		struct folio *folio = page_folio(page);

		if (folio == last)
			continue;
		last = folio;
		if (dirty)
			folio_mark_dirty(folio);
		if (backup)
			folio_mark_accessed(folio);

		if (!folio_batch_add(&fbatch, folio))
			check_release_folio_batch(&fbatch);
	}
	if (fbatch.nr)
		check_release_folio_batch(&fbatch);

	sg_free_table(st);
}

int shmem_sg_alloc_table(struct drm_i915_private *i915, struct sg_table *st,
			 size_t size, struct intel_memory_region *mr,
			 struct address_space *mapping,
			 unsigned int max_segment)
{
	unsigned int page_count; /* restricted by sg_alloc_table */
	unsigned long i;
	struct scatterlist *sg;
	unsigned long next_pfn = 0;	/* suppress gcc warning */
	gfp_t noreclaim;
	int ret;

	if (overflows_type(size / PAGE_SIZE, page_count))
		return -E2BIG;

	page_count = size / PAGE_SIZE;
	/*
	 * If there's no chance of allocating enough pages for the whole
	 * object, bail early.
	 */
	if (size > resource_size(&mr->region))
		return -ENOMEM;

	if (sg_alloc_table(st, page_count, GFP_KERNEL | __GFP_NOWARN))
		return -ENOMEM;

	/*
	 * Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping_set_unevictable(mapping);
	noreclaim = mapping_gfp_constraint(mapping, ~__GFP_RECLAIM);
	noreclaim |= __GFP_NORETRY | __GFP_NOWARN;

	sg = st->sgl;
	st->nents = 0;
	for (i = 0; i < page_count; i++) {
		struct folio *folio;
		unsigned long nr_pages;
		const unsigned int shrink[] = {
			I915_SHRINK_BOUND | I915_SHRINK_UNBOUND,
			0,
		}, *s = shrink;
		gfp_t gfp = noreclaim;

		do {
			cond_resched();
			folio = shmem_read_folio_gfp(mapping, i, gfp);
			if (!IS_ERR(folio))
				break;

			if (!*s) {
				ret = PTR_ERR(folio);
				goto err_sg;
			}

			i915_gem_shrink(NULL, i915, 2 * page_count, NULL, *s++);

			/*
			 * We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 *
			 * However, since graphics tend to be disposable,
			 * defer the oom here by reporting the ENOMEM back
			 * to userspace.
			 */
			if (!*s) {
				/* reclaim and warn, but no oom */
				gfp = mapping_gfp_mask(mapping);

				/*
				 * Our bo are always dirty and so we require
				 * kswapd to reclaim our pages (direct reclaim
				 * does not effectively begin pageout of our
				 * buffers on its own). However, direct reclaim
				 * only waits for kswapd when under allocation
				 * congestion. So as a result __GFP_RECLAIM is
				 * unreliable and fails to actually reclaim our
				 * dirty pages -- unless you try over and over
				 * again with !__GFP_NORETRY. However, we still
				 * want to fail this allocation rather than
				 * trigger the out-of-memory killer and for
				 * this we want __GFP_RETRY_MAYFAIL.
				 */
				gfp |= __GFP_RETRY_MAYFAIL | __GFP_NOWARN;
			}
		} while (1);

		nr_pages = min_t(unsigned long,
				folio_nr_pages(folio), page_count - i);
		if (!i ||
		    sg->length >= max_segment ||
		    folio_pfn(folio) != next_pfn) {
			if (i)
				sg = sg_next(sg);

			st->nents++;
			sg_set_folio(sg, folio, nr_pages * PAGE_SIZE, 0);
		} else {
			/* XXX: could overflow? */
			sg->length += nr_pages * PAGE_SIZE;
		}
		next_pfn = folio_pfn(folio) + nr_pages;
		i += nr_pages - 1;

		/* Check that the i965g/gm workaround works. */
		GEM_BUG_ON(gfp & __GFP_DMA32 && next_pfn >= 0x00100000UL);
	}
	if (sg) /* loop terminated early; short sg table */
		sg_mark_end(sg);

	/* Trim unused sg entries to avoid wasting memory. */
	i915_sg_trim(st);

	return 0;
err_sg:
	sg_mark_end(sg);
	if (sg != st->sgl) {
		shmem_sg_free_table(st, mapping, false, false);
	} else {
		mapping_clear_unevictable(mapping);
		sg_free_table(st);
	}

	/*
	 * shmemfs first checks if there is enough memory to allocate the page
	 * and reports ENOSPC should there be insufficient, along with the usual
	 * ENOMEM for a genuine allocation failure.
	 *
	 * We use ENOSPC in our driver to mean that we have run out of aperture
	 * space and so want to translate the error from shmemfs back to our
	 * usual understanding of ENOMEM.
	 */
	if (ret == -ENOSPC)
		ret = -ENOMEM;

	return ret;
}

static int shmem_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_memory_region *mem = obj->mm.region;
	struct address_space *mapping = obj->base.filp->f_mapping;
	unsigned int max_segment = i915_sg_segment_size(i915->drm.dev);
	struct sg_table *st;
	int ret;

	/*
	 * Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	GEM_BUG_ON(obj->read_domains & I915_GEM_GPU_DOMAINS);
	GEM_BUG_ON(obj->write_domain & I915_GEM_GPU_DOMAINS);

rebuild_st:
	st = kmalloc(sizeof(*st), GFP_KERNEL | __GFP_NOWARN);
	if (!st)
		return -ENOMEM;

	ret = shmem_sg_alloc_table(i915, st, obj->base.size, mem, mapping,
				   max_segment);
	if (ret)
		goto err_st;

	ret = i915_gem_gtt_prepare_pages(obj, st);
	if (ret) {
		/*
		 * DMA remapping failed? One possible cause is that
		 * it could not reserve enough large entries, asking
		 * for PAGE_SIZE chunks instead may be helpful.
		 */
		if (max_segment > PAGE_SIZE) {
			shmem_sg_free_table(st, mapping, false, false);
			kfree(st);

			max_segment = PAGE_SIZE;
			goto rebuild_st;
		} else {
			dev_warn(i915->drm.dev,
				 "Failed to DMA remap %zu pages\n",
				 obj->base.size >> PAGE_SHIFT);
			goto err_pages;
		}
	}

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj, st);

	if (i915_gem_object_can_bypass_llc(obj))
		obj->cache_dirty = true;

	__i915_gem_object_set_pages(obj, st);

	return 0;

err_pages:
	shmem_sg_free_table(st, mapping, false, false);
	/*
	 * shmemfs first checks if there is enough memory to allocate the page
	 * and reports ENOSPC should there be insufficient, along with the usual
	 * ENOMEM for a genuine allocation failure.
	 *
	 * We use ENOSPC in our driver to mean that we have run out of aperture
	 * space and so want to translate the error from shmemfs back to our
	 * usual understanding of ENOMEM.
	 */
err_st:
	if (ret == -ENOSPC)
		ret = -ENOMEM;

	kfree(st);

	return ret;
}

static int
shmem_truncate(struct drm_i915_gem_object *obj)
{
	/*
	 * Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	shmem_truncate_range(file_inode(obj->base.filp), 0, (loff_t)-1);
	obj->mm.madv = __I915_MADV_PURGED;
	obj->mm.pages = ERR_PTR(-EFAULT);

	return 0;
}

void __shmem_writeback(size_t size, struct address_space *mapping)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = SWAP_CLUSTER_MAX,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};
	struct folio *folio = NULL;
	int error = 0;

	/*
	 * Leave mmapings intact (GTT will have been revoked on unbinding,
	 * leaving only CPU mmapings around) and add those folios to the LRU
	 * instead of invoking writeback so they are aged and paged out
	 * as normal.
	 */
	while ((folio = writeback_iter(mapping, &wbc, folio, &error))) {
		if (folio_mapped(folio))
			folio_redirty_for_writepage(&wbc, folio);
		else
			error = shmem_writeout(folio, NULL, NULL);
	}
}

static void
shmem_writeback(struct drm_i915_gem_object *obj)
{
	__shmem_writeback(obj->base.size, obj->base.filp->f_mapping);
}

static int shmem_shrink(struct drm_i915_gem_object *obj, unsigned int flags)
{
	switch (obj->mm.madv) {
	case I915_MADV_DONTNEED:
		return i915_gem_object_truncate(obj);
	case __I915_MADV_PURGED:
		return 0;
	}

	if (flags & I915_GEM_OBJECT_SHRINK_WRITEBACK)
		shmem_writeback(obj);

	return 0;
}

void
__i915_gem_object_release_shmem(struct drm_i915_gem_object *obj,
				struct sg_table *pages,
				bool needs_clflush)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	GEM_BUG_ON(obj->mm.madv == __I915_MADV_PURGED);

	if (obj->mm.madv == I915_MADV_DONTNEED)
		obj->mm.dirty = false;

	if (needs_clflush &&
	    (obj->read_domains & I915_GEM_DOMAIN_CPU) == 0 &&
	    !(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
		drm_clflush_sg(pages);

	__start_cpu_write(obj);
	/*
	 * On non-LLC igfx platforms, force the flush-on-acquire if this is ever
	 * swapped-in. Our async flush path is not trust worthy enough yet(and
	 * happens in the wrong order), and with some tricks it's conceivable
	 * for userspace to change the cache-level to I915_CACHE_NONE after the
	 * pages are swapped-in, and since execbuf binds the object before doing
	 * the async flush, we have a race window.
	 */
	if (!HAS_LLC(i915) && !IS_DGFX(i915))
		obj->cache_dirty = true;
}

void i915_gem_object_put_pages_shmem(struct drm_i915_gem_object *obj, struct sg_table *pages)
{
	__i915_gem_object_release_shmem(obj, pages, true);

	i915_gem_gtt_finish_pages(obj, pages);

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj, pages);

	shmem_sg_free_table(pages, file_inode(obj->base.filp)->i_mapping,
			    obj->mm.dirty, obj->mm.madv == I915_MADV_WILLNEED);
	kfree(pages);
	obj->mm.dirty = false;
}

static void
shmem_put_pages(struct drm_i915_gem_object *obj, struct sg_table *pages)
{
	if (likely(i915_gem_object_has_struct_page(obj)))
		i915_gem_object_put_pages_shmem(obj, pages);
	else
		i915_gem_object_put_pages_phys(obj, pages);
}

static int
shmem_pwrite(struct drm_i915_gem_object *obj,
	     const struct drm_i915_gem_pwrite *arg)
{
	char __user *user_data = u64_to_user_ptr(arg->data_ptr);
	struct file *file = obj->base.filp;
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t written;
	u64 size = arg->size;

	/* Caller already validated user args */
	GEM_BUG_ON(!access_ok(user_data, arg->size));

	if (!i915_gem_object_has_struct_page(obj))
		return i915_gem_object_pwrite_phys(obj, arg);

	/*
	 * Before we instantiate/pin the backing store for our use, we
	 * can prepopulate the shmemfs filp efficiently using a write into
	 * the pagecache. We avoid the penalty of instantiating all the
	 * pages, important if the user is just writing to a few and never
	 * uses the object on the GPU, and using a direct write into shmemfs
	 * allows it to avoid the cost of retrieving a page (either swapin
	 * or clearing-before-use) before it is overwritten.
	 */
	if (i915_gem_object_has_pages(obj))
		return -ENODEV;

	if (obj->mm.madv != I915_MADV_WILLNEED)
		return -EFAULT;

	if (size > MAX_RW_COUNT)
		return -EFBIG;

	if (!file->f_op->write_iter)
		return -EINVAL;

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = arg->offset;
	iov_iter_ubuf(&iter, ITER_SOURCE, (void __user *)user_data, size);

	written = file->f_op->write_iter(&kiocb, &iter);
	BUG_ON(written == -EIOCBQUEUED);

	if (written != size)
		return -EIO;

	if (written < 0)
		return written;

	return 0;
}

static int
shmem_pread(struct drm_i915_gem_object *obj,
	    const struct drm_i915_gem_pread *arg)
{
	if (!i915_gem_object_has_struct_page(obj))
		return i915_gem_object_pread_phys(obj, arg);

	return -ENODEV;
}

static void shmem_release(struct drm_i915_gem_object *obj)
{
	if (i915_gem_object_has_struct_page(obj))
		i915_gem_object_release_memory_region(obj);

	fput(obj->base.filp);
}

const struct drm_i915_gem_object_ops i915_gem_shmem_ops = {
	.name = "i915_gem_object_shmem",
	.flags = I915_GEM_OBJECT_IS_SHRINKABLE,

	.get_pages = shmem_get_pages,
	.put_pages = shmem_put_pages,
	.truncate = shmem_truncate,
	.shrink = shmem_shrink,

	.pwrite = shmem_pwrite,
	.pread = shmem_pread,

	.release = shmem_release,
};

static int __create_shmem(struct drm_i915_private *i915,
			  struct drm_gem_object *obj,
			  resource_size_t size)
{
	unsigned long flags = VM_NORESERVE;
	struct file *filp;

	drm_gem_private_object_init(&i915->drm, obj, size);

	/* XXX: The __shmem_file_setup() function returns -EINVAL if size is
	 * greater than MAX_LFS_FILESIZE.
	 * To handle the same error as other code that returns -E2BIG when
	 * the size is too large, we add a code that returns -E2BIG when the
	 * size is larger than the size that can be handled.
	 * If BITS_PER_LONG is 32, size > MAX_LFS_FILESIZE is always false,
	 * so we only needs to check when BITS_PER_LONG is 64.
	 * If BITS_PER_LONG is 32, E2BIG checks are processed when
	 * i915_gem_object_size_2big() is called before init_object() callback
	 * is called.
	 */
	if (BITS_PER_LONG == 64 && size > MAX_LFS_FILESIZE)
		return -E2BIG;

	if (i915->mm.gemfs)
		filp = shmem_file_setup_with_mnt(i915->mm.gemfs, "i915", size,
						 flags);
	else
		filp = shmem_file_setup("i915", size, flags);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	/*
	 * Prevent -EFBIG by allowing large writes beyond MAX_NON_LFS on shmem
	 * objects by setting O_LARGEFILE.
	 */
	if (force_o_largefile())
		filp->f_flags |= O_LARGEFILE;

	obj->filp = filp;
	return 0;
}

static int shmem_object_init(struct intel_memory_region *mem,
			     struct drm_i915_gem_object *obj,
			     resource_size_t offset,
			     resource_size_t size,
			     resource_size_t page_size,
			     unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	struct address_space *mapping;
	unsigned int cache_level;
	gfp_t mask;
	int ret;

	ret = __create_shmem(i915, &obj->base, size);
	if (ret)
		return ret;

	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
	if (IS_I965GM(i915) || IS_I965G(i915)) {
		/* 965gm cannot relocate objects above 4GiB. */
		mask &= ~__GFP_HIGHMEM;
		mask |= __GFP_DMA32;
	}

	mapping = obj->base.filp->f_mapping;
	mapping_set_gfp_mask(mapping, mask);
	GEM_BUG_ON(!(mapping_gfp_mask(mapping) & __GFP_RECLAIM));

	i915_gem_object_init(obj, &i915_gem_shmem_ops, &lock_class, flags);
	obj->mem_flags |= I915_BO_FLAG_STRUCT_PAGE;
	obj->write_domain = I915_GEM_DOMAIN_CPU;
	obj->read_domains = I915_GEM_DOMAIN_CPU;

	/*
	 * MTL doesn't snoop CPU cache by default for GPU access (namely
	 * 1-way coherency). However some UMD's are currently depending on
	 * that. Make 1-way coherent the default setting for MTL. A follow
	 * up patch will extend the GEM_CREATE uAPI to allow UMD's specify
	 * caching mode at BO creation time
	 */
	if (HAS_LLC(i915) || (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70)))
		/* On some devices, we can have the GPU use the LLC (the CPU
		 * cache) for about a 10% performance improvement
		 * compared to uncached.  Graphics requests other than
		 * display scanout are coherent with the CPU in
		 * accessing this cache.  This means in this mode we
		 * don't need to clflush on the CPU side, and on the
		 * GPU side we only need to flush internal caches to
		 * get data visible to the CPU.
		 *
		 * However, we maintain the display planes as UC, and so
		 * need to rebind when first used as such.
		 */
		cache_level = I915_CACHE_LLC;
	else
		cache_level = I915_CACHE_NONE;

	i915_gem_object_set_cache_coherency(obj, cache_level);

	i915_gem_object_init_memory_region(obj, mem);

	return 0;
}

struct drm_i915_gem_object *
i915_gem_object_create_shmem(struct drm_i915_private *i915,
			     resource_size_t size)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_SMEM],
					     size, 0, 0);
}

/* Allocate a new GEM object and fill it with the supplied data */
struct drm_i915_gem_object *
i915_gem_object_create_shmem_from_data(struct drm_i915_private *i915,
				       const void *data, resource_size_t size)
{
	struct drm_i915_gem_object *obj;
	struct file *file;
	loff_t pos = 0;
	ssize_t err;

	GEM_WARN_ON(IS_DGFX(i915));
	obj = i915_gem_object_create_shmem(i915, round_up(size, PAGE_SIZE));
	if (IS_ERR(obj))
		return obj;

	GEM_BUG_ON(obj->write_domain != I915_GEM_DOMAIN_CPU);

	file = obj->base.filp;
	err = kernel_write(file, data, size, &pos);

	if (err < 0)
		goto fail;

	if (err != size) {
		err = -EIO;
		goto fail;
	}

	return obj;

fail:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static int init_shmem(struct intel_memory_region *mem)
{
	i915_gemfs_init(mem->i915);
	intel_memory_region_set_name(mem, "system");

	return 0; /* We have fallback to the kernel mnt if gemfs init failed. */
}

static int release_shmem(struct intel_memory_region *mem)
{
	i915_gemfs_fini(mem->i915);
	return 0;
}

static const struct intel_memory_region_ops shmem_region_ops = {
	.init = init_shmem,
	.release = release_shmem,
	.init_object = shmem_object_init,
};

struct intel_memory_region *i915_gem_shmem_setup(struct drm_i915_private *i915,
						 u16 type, u16 instance)
{
	return intel_memory_region_create(i915, 0,
					  totalram_pages() << PAGE_SHIFT,
					  PAGE_SIZE, 0, 0,
					  type, instance,
					  &shmem_region_ops);
}

bool i915_gem_object_is_shmem(const struct drm_i915_gem_object *obj)
{
	return obj->ops == &i915_gem_shmem_ops;
}
