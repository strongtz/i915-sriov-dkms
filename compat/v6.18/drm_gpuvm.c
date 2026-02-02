/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>

#include <drm/drm_gpuvm.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
static struct drm_gpuva_op *
gpuva_op_alloc(struct drm_gpuvm *gpuvm)
{
	const struct drm_gpuvm_ops *fn = gpuvm->ops;
	struct drm_gpuva_op *op;

	if (fn && fn->op_alloc)
		op = fn->op_alloc();
	else
		op = kzalloc(sizeof(*op), GFP_KERNEL);

	if (unlikely(!op))
		return NULL;

	return op;
}

static void
gpuva_op_free(struct drm_gpuvm *gpuvm,
	      struct drm_gpuva_op *op)
{
	const struct drm_gpuvm_ops *fn = gpuvm->ops;

	if (fn && fn->op_free)
		fn->op_free(op);
	else
		kfree(op);
}

static int
drm_gpuva_sm_step(struct drm_gpuva_op *__op,
		  void *priv)
{
	struct {
		struct drm_gpuvm *vm;
		struct drm_gpuva_ops *ops;
	} *args = priv;
	struct drm_gpuvm *gpuvm = args->vm;
	struct drm_gpuva_ops *ops = args->ops;
	struct drm_gpuva_op *op;

	op = gpuva_op_alloc(gpuvm);
	if (unlikely(!op))
		goto err;

	memcpy(op, __op, sizeof(*op));

	if (op->op == DRM_GPUVA_OP_REMAP) {
		struct drm_gpuva_op_remap *__r = &__op->remap;
		struct drm_gpuva_op_remap *r = &op->remap;

		r->unmap = kmemdup(__r->unmap, sizeof(*r->unmap),
				   GFP_KERNEL);
		if (unlikely(!r->unmap))
			goto err_free_op;

		if (__r->prev) {
			r->prev = kmemdup(__r->prev, sizeof(*r->prev),
					  GFP_KERNEL);
			if (unlikely(!r->prev))
				goto err_free_unmap;
		}

		if (__r->next) {
			r->next = kmemdup(__r->next, sizeof(*r->next),
					  GFP_KERNEL);
			if (unlikely(!r->next))
				goto err_free_prev;
		}
	}

	list_add_tail(&op->entry, &ops->list);

	return 0;

err_free_unmap:
	kfree(op->remap.unmap);
err_free_prev:
	kfree(op->remap.prev);
err_free_op:
	gpuva_op_free(gpuvm, op);
err:
	return -ENOMEM;
}

static const struct drm_gpuvm_ops gpuvm_list_ops = {
	.sm_step_map = drm_gpuva_sm_step,
	.sm_step_remap = drm_gpuva_sm_step,
	.sm_step_unmap = drm_gpuva_sm_step,
};

static int
op_map_cb(const struct drm_gpuvm_ops *fn, void *priv,
	  const struct drm_gpuvm_map_req *req)
{
	struct drm_gpuva_op op = {};

	if (!req)
		return 0;

	op.op = DRM_GPUVA_OP_MAP;
	op.map.va.addr = req->map.va.addr;
	op.map.va.range = req->map.va.range;
	op.map.gem.obj = req->map.gem.obj;
	op.map.gem.offset = req->map.gem.offset;

	return fn->sm_step_map(&op, priv);
}

static int
op_remap_cb(const struct drm_gpuvm_ops *fn, void *priv,
	    struct drm_gpuva_op_map *prev,
	    struct drm_gpuva_op_map *next,
	    struct drm_gpuva_op_unmap *unmap)
{
	struct drm_gpuva_op op = {};
	struct drm_gpuva_op_remap *r;

	op.op = DRM_GPUVA_OP_REMAP;
	r = &op.remap;
	r->prev = prev;
	r->next = next;
	r->unmap = unmap;

	return fn->sm_step_remap(&op, priv);
}

static int
op_unmap_cb(const struct drm_gpuvm_ops *fn, void *priv,
	    struct drm_gpuva *va, bool merge, bool madvise)
{
	struct drm_gpuva_op op = {};

	if (madvise)
		return 0;

	op.op = DRM_GPUVA_OP_UNMAP;
	op.unmap.va = va;
	op.unmap.keep = merge;

	return fn->sm_step_unmap(&op, priv);
}

static int
__drm_gpuvm_sm_map(struct drm_gpuvm *gpuvm,
		   const struct drm_gpuvm_ops *ops, void *priv,
		   const struct drm_gpuvm_map_req *req,
		   bool madvise)
{
	struct drm_gem_object *req_obj = req->map.gem.obj;
	const struct drm_gpuvm_map_req *op_map = madvise ? NULL : req;
	struct drm_gpuva *va, *next;
	u64 req_offset = req->map.gem.offset;
	u64 req_range = req->map.va.range;
	u64 req_addr = req->map.va.addr;
	u64 req_end = req_addr + req_range;
	int ret;

	if (unlikely(!drm_gpuvm_range_valid(gpuvm, req_addr, req_range)))
		return -EINVAL;

	drm_gpuvm_for_each_va_range_safe(va, next, gpuvm, req_addr, req_end) {
		struct drm_gem_object *obj = va->gem.obj;
		u64 offset = va->gem.offset;
		u64 addr = va->va.addr;
		u64 range = va->va.range;
		u64 end = addr + range;
		bool merge = !!va->gem.obj;

		if (madvise && obj)
			continue;

		if (addr == req_addr) {
			merge &= obj == req_obj &&
				 offset == req_offset;

			if (end == req_end) {
				ret = op_unmap_cb(ops, priv, va, merge, madvise);
				if (ret)
					return ret;
				break;
			}

			if (end < req_end) {
				ret = op_unmap_cb(ops, priv, va, merge, madvise);
				if (ret)
					return ret;
				continue;
			}

			if (end > req_end) {
				struct drm_gpuva_op_map n = {
					.va.addr = req_end,
					.va.range = range - req_range,
					.gem.obj = obj,
					.gem.offset = offset + req_range,
				};
				struct drm_gpuva_op_unmap u = {
					.va = va,
					.keep = merge,
				};

				ret = op_remap_cb(ops, priv, NULL, &n, &u);
				if (ret)
					return ret;

				if (madvise)
					op_map = req;
				break;
			}
		} else if (addr < req_addr) {
			u64 ls_range = req_addr - addr;
			struct drm_gpuva_op_map p = {
				.va.addr = addr,
				.va.range = ls_range,
				.gem.obj = obj,
				.gem.offset = offset,
			};
			struct drm_gpuva_op_unmap u = { .va = va };

			merge &= obj == req_obj &&
				 offset + ls_range == req_offset;
			u.keep = merge;

			if (end == req_end) {
				ret = op_remap_cb(ops, priv, &p, NULL, &u);
				if (ret)
					return ret;

				if (madvise)
					op_map = req;
				break;
			}

			if (end < req_end) {
				ret = op_remap_cb(ops, priv, &p, NULL, &u);
				if (ret)
					return ret;

				if (madvise) {
					struct drm_gpuvm_map_req map_req = {
						.map.va.addr =  req_addr,
						.map.va.range = end - req_addr,
					};

					ret = op_map_cb(ops, priv, &map_req);
					if (ret)
						return ret;
				}

				continue;
			}

			if (end > req_end) {
				struct drm_gpuva_op_map n = {
					.va.addr = req_end,
					.va.range = end - req_end,
					.gem.obj = obj,
					.gem.offset = offset + ls_range +
						      req_range,
				};

				ret = op_remap_cb(ops, priv, &p, &n, &u);
				if (ret)
					return ret;

				if (madvise)
					op_map = req;
				break;
			}
		} else if (addr > req_addr) {
			merge &= obj == req_obj &&
				 offset == req_offset +
					   (addr - req_addr);

			if (end == req_end) {
				ret = op_unmap_cb(ops, priv, va, merge, madvise);
				if (ret)
					return ret;

				break;
			}

			if (end < req_end) {
				ret = op_unmap_cb(ops, priv, va, merge, madvise);
				if (ret)
					return ret;

				continue;
			}

			if (end > req_end) {
				struct drm_gpuva_op_map n = {
					.va.addr = req_end,
					.va.range = end - req_end,
					.gem.obj = obj,
					.gem.offset = offset + req_end - addr,
				};
				struct drm_gpuva_op_unmap u = {
					.va = va,
					.keep = merge,
				};

				ret = op_remap_cb(ops, priv, NULL, &n, &u);
				if (ret)
					return ret;

				if (madvise) {
					struct drm_gpuvm_map_req map_req = {
						.map.va.addr =  addr,
						.map.va.range = req_end - addr,
					};

					return op_map_cb(ops, priv, &map_req);
				}
				break;
			}
		}
	}
	return op_map_cb(ops, priv, op_map);
}

static struct drm_gpuva_ops *
__drm_gpuvm_sm_map_ops_create(struct drm_gpuvm *gpuvm,
			      const struct drm_gpuvm_map_req *req,
			      bool madvise)
{
	struct drm_gpuva_ops *ops;
	struct {
		struct drm_gpuvm *vm;
		struct drm_gpuva_ops *ops;
	} args;
	int ret;

	ops = kzalloc(sizeof(*ops), GFP_KERNEL);
	if (unlikely(!ops))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ops->list);

	args.vm = gpuvm;
	args.ops = ops;

	ret = __drm_gpuvm_sm_map(gpuvm, &gpuvm_list_ops, &args, req, madvise);
	if (ret)
		goto err_free_ops;

	return ops;

err_free_ops:
	drm_gpuva_ops_free(gpuvm, ops);
	return ERR_PTR(ret);
}

struct drm_gpuva_ops *
drm_gpuvm_madvise_ops_create(struct drm_gpuvm *gpuvm,
			     const struct drm_gpuvm_map_req *req)
{
	return __drm_gpuvm_sm_map_ops_create(gpuvm, req, true);
}
EXPORT_SYMBOL_GPL(drm_gpuvm_madvise_ops_create);
#endif
