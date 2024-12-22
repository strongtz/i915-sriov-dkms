// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#include "selftests/mock_gem_device.h"

#define MOCK_NUM_VFS	7

#define for_each_ggtt_page(ggtt_addr, node) \
	for ((ggtt_addr) = (node)->start; \
	     (ggtt_addr) < (node)->size; \
	     (ggtt_addr) += I915_GTT_PAGE_SIZE_4K)

static int mock_update_ptes(struct intel_iov *iov, struct sg_table *st, gen8_pte_t pte_pattern)
{
	gen8_pte_t *ptes = iov->pf.ggtt.selftest.ptes;
	dma_addr_t addr;
	struct sgt_iter iter;

	for_each_sgt_daddr(addr, iter, st)
		*(ptes++) = pte_pattern | addr;

	return 0;
}

static gen8_pte_t make_pte(u64 seed, unsigned int vfid)
{
	return FIELD_PREP(MTL_GGTT_PTE_PAT_MASK, seed) |
	       FIELD_PREP(GEN12_GGTT_PTE_ADDR_MASK, seed) |
	       i915_ggtt_prepare_vf_pte(vfid);
}

static struct drm_mm_node *mock_provisioning_ggtt_init(struct intel_iov *iov, unsigned int vfid,
						       u64 start, u64 size)
{
	struct drm_mm_node *node = &iov->pf.provisioning.configs[vfid].ggtt_region;

	node->start = start;
	node->size = size;
	set_bit(DRM_MM_NODE_ALLOCATED_BIT, &node->flags);

	return node;
}

static int mock_provisioning_configs_init(struct intel_iov *iov)
{
	struct intel_iov_config *configs;

	GEM_BUG_ON(iov->pf.provisioning.configs);

	configs = kcalloc(1 + pf_get_totalvfs(iov), sizeof(*configs), GFP_KERNEL);
	if (!configs)
		return -ENOMEM;

	iov->pf.provisioning.configs = configs;

	return 0;
}

static void mock_provisioning_configs_fini(struct intel_iov *iov)
{
	kfree(iov->pf.provisioning.configs);
	iov->pf.provisioning.configs = NULL;
}

static int mock_ggtt_shadow_init_test(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_gt(iov)->i915;
	int err;

	i915->__mode = I915_IOV_MODE_SRIOV_PF;
	i915->sriov.pf.driver_vfs = MOCK_NUM_VFS;

	err = mock_provisioning_configs_init(iov);
	if (err < 0)
		return err;

	err = intel_iov_ggtt_shadow_init(iov);

	return err;
}

static void mock_ggtt_shadow_fini_test(struct intel_iov *iov)
{
	struct drm_i915_private *i915 = iov_to_gt(iov)->i915;

	i915->__mode = I915_IOV_MODE_NONE;
	i915->sriov.pf.driver_vfs = 0;

	intel_iov_ggtt_shadow_fini(iov);
	mock_provisioning_configs_fini(iov);
}

static int mock_ggtt_shadow_basic(void *arg)
{
	struct intel_iov *iov = arg;
	const unsigned int vfid = VFID(1);
	struct drm_mm_node *node;
	u64 ggtt_addr;
	gen8_pte_t *shadow_ggtt;
	int err;

	err = mock_ggtt_shadow_init_test(iov);
	if (err < 0)
		return err;

	node = mock_provisioning_ggtt_init(iov, vfid, 0, SZ_4G);

	err = intel_iov_ggtt_shadow_vf_alloc(iov, vfid, node);
	if (err < 0)
		goto out;

	for_each_ggtt_page(ggtt_addr, node)
		intel_iov_ggtt_shadow_set_pte(iov, vfid, ggtt_addr, make_pte(ggtt_addr, vfid));

	shadow_ggtt = iov->pf.ggtt.shadows_ggtt[vfid].ptes;
	for_each_ggtt_page(ggtt_addr, node) {
		gen8_pte_t current_pte = *(shadow_ggtt++);
		gen8_pte_t expected_pte = make_pte(ggtt_addr, vfid);

		if (current_pte != expected_pte) {
			IOV_SELFTEST_ERROR(iov,
					   "PTE value in the shadow GGTT address %#llx: expected: %llx current: %#llx\n",
					   ggtt_addr, current_pte, expected_pte);
			err = -EINVAL;
			break;
		}
	}

	intel_iov_ggtt_shadow_vf_free(iov, vfid);
out:
	mock_ggtt_shadow_fini_test(iov);
	return err;
}

static int mock_ggtt_shadow_save_basic(void *arg)
{
	struct intel_iov *iov = arg;
	const unsigned int vfid = VFID(1);
	struct drm_mm_node *node;
	gen8_pte_t *buf, *origin_buf;
	size_t size;
	u64 ggtt_addr;
	int err;

	err = mock_ggtt_shadow_init_test(iov);
	if (err < 0)
		return err;

	node = mock_provisioning_ggtt_init(iov, vfid, 0, SZ_4G);

	size = ggtt_size_to_ptes_size(node->size);
	origin_buf = kvzalloc(size, GFP_KERNEL);
	if (!origin_buf) {
		err = -ENOMEM;
		goto out_fini;
	}

	err = intel_iov_ggtt_shadow_vf_alloc(iov, vfid, node);
	if (err < 0)
		goto out_free;

	for_each_ggtt_page(ggtt_addr, node)
		intel_iov_ggtt_shadow_set_pte(iov, vfid, ggtt_addr, make_pte(ggtt_addr, vfid));

	intel_iov_ggtt_shadow_save(iov, vfid, origin_buf, size, 0);

	buf = origin_buf;
	for_each_ggtt_page(ggtt_addr, node) {
		gen8_pte_t current_pte = *(buf++);
		gen8_pte_t expected_pte = make_pte(ggtt_addr, vfid);

		if (current_pte != expected_pte) {
			IOV_SELFTEST_ERROR(iov,
					   "PTE value in the saved buffer for GGTT address %#llx: expected: %llx current: %#llx\n",
					   ggtt_addr, current_pte, expected_pte);
			err = -EINVAL;
			break;
		}
	}

	intel_iov_ggtt_shadow_vf_free(iov, vfid);
out_free:
	kvfree(origin_buf);
out_fini:
	mock_ggtt_shadow_fini_test(iov);
	return err;
}

static int mock_ggtt_shadow_save_no_vfid(void *arg)
{
	struct intel_iov *iov = arg;
	const unsigned int vfid = VFID(1);
	struct drm_mm_node *node;
	gen8_pte_t *buf, *origin_buf;
	size_t size;
	u64 ggtt_addr;
	int err;

	err = mock_ggtt_shadow_init_test(iov);
	if (err < 0)
		return err;

	node = mock_provisioning_ggtt_init(iov, vfid, 0, SZ_4G);

	size = ggtt_size_to_ptes_size(node->size);
	origin_buf = kvzalloc(size, GFP_KERNEL);
	if (!origin_buf) {
		err = -ENOMEM;
		goto out_fini;
	}

	err = intel_iov_ggtt_shadow_vf_alloc(iov, vfid, node);
	if (err < 0)
		goto out_free;

	for_each_ggtt_page(ggtt_addr, node)
		intel_iov_ggtt_shadow_set_pte(iov, vfid, ggtt_addr, make_pte(ggtt_addr, vfid));

	intel_iov_ggtt_shadow_save(iov, vfid, origin_buf, size, I915_GGTT_SAVE_PTES_NO_VFID);

	buf = origin_buf;
	for_each_ggtt_page(ggtt_addr, node) {
		gen8_pte_t current_pte = *(buf++);
		gen8_pte_t expected_pte = make_pte(ggtt_addr, 0);

		if (current_pte != expected_pte) {
			IOV_SELFTEST_ERROR(iov,
					   "PTE value in the saved buffer for GGTT address %#llx: expected: %llx current: %#llx\n",
					   ggtt_addr, current_pte, expected_pte);
			err = -EINVAL;
			break;
		}
	}

	intel_iov_ggtt_shadow_vf_free(iov, vfid);
out_free:
	kvfree(origin_buf);
out_fini:
	mock_ggtt_shadow_fini_test(iov);
	return err;
}

static int mock_ggtt_shadow_restore_basic(void *arg)
{
	struct intel_iov *iov = arg;
	const unsigned int vfid = VFID(1);
	struct drm_mm_node *node;
	gen8_pte_t *buf, *origin_buf, *shadow_ggtt, *hw_ptes;
	size_t size;
	u64 ggtt_addr;
	int err;

	err = mock_ggtt_shadow_init_test(iov);
	if (err < 0)
		return err;

	node = mock_provisioning_ggtt_init(iov, vfid, 0, SZ_4G);

	size = ggtt_size_to_ptes_size(node->size);

	iov->pf.ggtt.selftest.mock_update_ptes = mock_update_ptes;
	iov->pf.ggtt.selftest.ptes = kvzalloc(size, GFP_KERNEL);
	if (!iov->pf.ggtt.selftest.ptes) {
		err = -ENOMEM;
		goto out_fini;
	}

	origin_buf = kvzalloc(size, GFP_KERNEL);
	if (!origin_buf) {
		err = -ENOMEM;
		goto out_free_ptes;
	}

	err = intel_iov_ggtt_shadow_vf_alloc(iov, vfid, node);
	if (err < 0)
		goto out_free_buf;

	buf = origin_buf;
	for_each_ggtt_page(ggtt_addr, node)
		*(buf++) = make_pte(ggtt_addr, vfid);

	intel_iov_ggtt_shadow_restore(iov, vfid, origin_buf, size, 0);

	shadow_ggtt = iov->pf.ggtt.shadows_ggtt[vfid].ptes;
	hw_ptes = iov->pf.ggtt.selftest.ptes;
	for_each_ggtt_page(ggtt_addr, node) {
		gen8_pte_t current_shadow_pte = *(shadow_ggtt++);
		gen8_pte_t current_hw_pte = *(hw_ptes++);
		gen8_pte_t expected_pte = make_pte(ggtt_addr, vfid);

		if (current_shadow_pte != expected_pte || current_hw_pte != expected_pte) {
			IOV_SELFTEST_ERROR(iov,
					   "PTE values for GGTT address %llx, not match with expected value, expected: %#llx from shadow GGTT: %#llx, from HW: %#llx\n",
					   ggtt_addr, expected_pte, current_shadow_pte,
					   current_hw_pte);
			err = -EINVAL;
			break;
		}
	}

	intel_iov_ggtt_shadow_vf_free(iov, vfid);
out_free_buf:
	kvfree(origin_buf);
out_free_ptes:
	kvfree(iov->pf.ggtt.selftest.ptes);
	iov->pf.ggtt.selftest.ptes = NULL;
out_fini:
	mock_ggtt_shadow_fini_test(iov);
	return err;
}

static int mock_ggtt_shadow_restore_new_vfid(void *arg)
{
	struct intel_iov *iov = arg;
	const u64 start_ggtt = 0;
	const size_t ggtt_size = SZ_4G;
	const unsigned int vfid = VFID(1);
	const unsigned int new_vfid = VFID(2);
	struct drm_mm_node *old_node, *new_node;
	gen8_pte_t *buf, *origin_buf, *shadow_ggtt, *hw_ptes;
	size_t size;
	u64 ggtt_addr;
	int err;

	err = mock_ggtt_shadow_init_test(iov);
	if (err < 0)
		return err;

	old_node = mock_provisioning_ggtt_init(iov, vfid, start_ggtt, ggtt_size);
	new_node = mock_provisioning_ggtt_init(iov, new_vfid, start_ggtt, ggtt_size);

	size = ggtt_size_to_ptes_size(ggtt_size);

	iov->pf.ggtt.selftest.mock_update_ptes = mock_update_ptes;
	iov->pf.ggtt.selftest.ptes = kvzalloc(size, GFP_KERNEL);
	if (!iov->pf.ggtt.selftest.ptes) {
		err = -ENOMEM;
		goto out_fini;
	}

	origin_buf = kvzalloc(size, GFP_KERNEL);
	if (!origin_buf) {
		err = -ENOMEM;
		goto out_free_ptes;
	}

	err = intel_iov_ggtt_shadow_vf_alloc(iov, vfid, old_node);
	if (err < 0)
		goto out_free_buf;

	err = intel_iov_ggtt_shadow_vf_alloc(iov, new_vfid, new_node);
	if (err < 0)
		goto out_free_vf;

	buf = origin_buf;
	for_each_ggtt_page(ggtt_addr, old_node)
		*(buf++) = make_pte(ggtt_addr, 0);

	intel_iov_ggtt_shadow_restore(iov, new_vfid, origin_buf, size,
					   FIELD_PREP(I915_GGTT_RESTORE_PTES_VFID_MASK, new_vfid) |
					   I915_GGTT_RESTORE_PTES_NEW_VFID);

	shadow_ggtt = iov->pf.ggtt.shadows_ggtt[new_vfid].ptes;
	hw_ptes = iov->pf.ggtt.selftest.ptes;
	for_each_ggtt_page(ggtt_addr, new_node) {
		gen8_pte_t current_shadow_pte = *(shadow_ggtt++);
		gen8_pte_t current_hw_pte = *(hw_ptes++);
		gen8_pte_t expected_pte = make_pte(ggtt_addr, new_vfid);

		if (current_shadow_pte != expected_pte || current_hw_pte != expected_pte) {
			IOV_SELFTEST_ERROR(iov,
					   "PTE values for GGTT address %llx, not match with expected value, expected: %#llx from shadow GGTT: %#llx, from HW: %#llx\n",
					   ggtt_addr, expected_pte, current_shadow_pte,
					   current_hw_pte);
			err = -EINVAL;
			break;
		}
	}

	intel_iov_ggtt_shadow_vf_free(iov, new_vfid);
out_free_vf:
	intel_iov_ggtt_shadow_vf_free(iov, vfid);
out_free_buf:
	kvfree(origin_buf);
out_free_ptes:
	kvfree(iov->pf.ggtt.selftest.ptes);
	iov->pf.ggtt.selftest.ptes = NULL;
out_fini:
	mock_ggtt_shadow_fini_test(iov);
	return err;
}

int selftest_mock_iov_ggtt(void)
{
	static const struct i915_subtest mock_tests[] = {
		SUBTEST(mock_ggtt_shadow_basic),
		SUBTEST(mock_ggtt_shadow_save_basic),
		SUBTEST(mock_ggtt_shadow_save_no_vfid),
		SUBTEST(mock_ggtt_shadow_restore_basic),
		SUBTEST(mock_ggtt_shadow_restore_new_vfid),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(mock_tests, &to_gt(i915)->iov);

	mock_destroy_device(i915);

	return err;
}
