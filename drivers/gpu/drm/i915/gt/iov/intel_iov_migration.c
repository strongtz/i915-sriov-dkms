// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_mm.h>

#include "gt/intel_gtt.h"
#include "intel_iov_migration.h"
#include "intel_iov_query.h"
#include "intel_iov_utils.h"
#include "intel_iov.h"

/**
 * intel_iov_migration_reinit_guc - Re-initialize GuC communication.
 * @iov: the iov struct
 *
 * After migration, we need to reestablish communication with GuC and
 * re-query all VF configuration to make sure they match previous
 * provisioning. Note that most of VF provisioning shall be the same,
 * except GGTT range, since GGTT is not virtualized per-VF.
 *
 * Returns: 0 if the operation completed successfully, or a negative error
 * code otherwise.
 */
int intel_iov_migration_reinit_guc(struct intel_iov *iov)
{
	int err;
	const char *where;

	err = intel_iov_query_config(iov);
	if (unlikely(err)) {
		where = "query config";
		goto fail;
	}

	return 0;

fail:
	IOV_ERROR(iov, "GuC re-init failed on %s (%pe)\n",
		  where, ERR_PTR(err));
	return err;
}

static u64 drm_mm_node_end(struct drm_mm_node *node)
{
	return node->start + node->size;
}

static s64 vf_get_post_migration_ggtt_shift(struct intel_iov *iov)
{
	u64 old_base;
	s64 ggtt_shift;

	old_base = drm_mm_node_end(&iov->vf.ggtt_balloon[0]);
	ggtt_shift = iov->vf.config.ggtt_base - (s64)old_base;
	iov->vf.config.ggtt_shift = ggtt_shift;

	IOV_DEBUG(iov, "GGTT base shifted from %#llx to %#llx\n",
		  old_base, old_base + ggtt_shift);

	return ggtt_shift;
}

static void i915_ggtt_shift_nodes(struct i915_ggtt *ggtt, struct drm_mm_node balloon_nodes[2],
				   s64 shift)
{
	struct drm_mm_node *node, *tmpn;
	int err;
	LIST_HEAD(temp_list_head);

	lockdep_assert_held(&ggtt->vm.mutex);

	/*
	 * Move nodes, from range previously assigned to this VF, into temp list.
	 *
	 * The balloon_nodes array contains two nodes: first which reserves the GGTT area
	 * below the range for current VF, and second which reserves area above. There
	 * may also exist extra nodes at the bottom or top of GGTT range, as long as
	 * there are no free spaces inbetween. Such extra nodes will be left unchanged.
	 *
	 * Below is a GGTT layout of example VF, with a certain address range assigned to
	 * said VF, and inaccessible areas above and below:
	 *
	 *  0                                                                   vm->total
	 *  |<--------------------------- Total GGTT size ----------------------------->|
	 *
	 *  +-----------+-------------------------+----------+--------------+-----------+
	 *  |\\\\\\\\\\\|/////////////////////////|  VF mem  |//////////////|\\\\\\\\\\\|
	 *  +-----------+-------------------------+----------+--------------+-----------+
	 *
	 * Hardware enforced access rules before migration:
	 *
	 *  |<------- inaccessible for VF ------->|<VF owned>|<-- inaccessible for VF ->|
	 *
	 * drm_mm nodes used for tracking allocations:
	 *
	 *  |<- extra ->|<------- balloon ------->|<- nodes->|<-- balloon ->|<- extra ->|
	 *
	 * After the migration, GGTT area assigned to the VF might have shifted, either
	 * to lower or to higher address. But we expect the total size and extra areas to
	 * be identical, as migration can only happen between matching platforms.
	 * Below is an example of GGTT layout of the VF after migration. Content of the
	 * GGTT for VF has been moved to a new area, and we receive its address from GuC:
	 *
	 *  +-----------+--------------+----------+-------------------------+-----------+
	 *  |\\\\\\\\\\\|//////////////|  VF mem  |/////////////////////////|\\\\\\\\\\\|
	 *  +-----------+--------------+----------+-------------------------+-----------+
	 *
	 * Hardware enforced access rules after migration:
	 *
	 *  |<- inaccessible for VF -->|<VF owned>|<------- inaccessible for VF ------->|
	 *
	 * So the VF has a new slice of GGTT assigned, and during migration process, the
	 * memory content was copied to that new area. But the drm_mm nodes within i915
	 * are still tracking allocations using the old addresses. The nodes within VF
	 * owned area have to be shifted, and balloon nodes need to be resized to
	 * properly mask out areas not owned by the VF.
	 *
	 * Fixed drm_mm nodes used for tracking allocations:
	 *
	 *  |<- extra  ->|<- balloon ->|<-- VF -->|<-------- balloon ------>|<- extra ->|
	 *
	 * Due to use of GPU profiles, we do not expect the old and new GGTT ares to
	 * overlap; but our node shifting will fix addresses properly regardless.
	 *
	 */
	drm_mm_for_each_node_in_range_safe(node, tmpn, &ggtt->vm.mm,
					   drm_mm_node_end(&balloon_nodes[0]),
					   balloon_nodes[1].start) {
		drm_mm_remove_node(node);
		list_add(&node->node_list, &temp_list_head);
	}

	/* shift and re-add ballooning nodes */
	for (node = &balloon_nodes[0]; node <= &balloon_nodes[1]; node++) {
		if (!drm_mm_node_allocated(node))
			continue;
		drm_mm_remove_node(node);
	}
	balloon_nodes[0].size += shift;
	balloon_nodes[1].start += shift;
	balloon_nodes[1].size -= shift;
	for (node = &balloon_nodes[0]; node <= &balloon_nodes[1]; node++) {
		if (node->size == 0)
			continue;
		err = drm_mm_reserve_node(&ggtt->vm.mm, node);
		GEM_BUG_ON(err);
	}

	/*
	 * Now the GGTT VM contains only nodes outside of area assigned to this VF.
	 * We can re-add all VF nodes with shifted offsets.
	 */
	list_for_each_entry_safe(node, tmpn, &temp_list_head, node_list) {
		list_del(&node->node_list);
		node->start += shift;
		err = drm_mm_reserve_node(&ggtt->vm.mm, node);
		GEM_BUG_ON(err);
	}
}

/**
 * intel_iov_migration_fixup_ggtt_nodes - Shift GGTT allocations to match assigned range.
 * @iov: the iov struct
 *
 * Since Global GTT is not virtualized, each VF has an assigned range
 * within the global space. This range might have changed during migration,
 * which requires all memory addresses pointing to GGTT to be shifted.
 */
void intel_iov_migration_fixup_ggtt_nodes(struct intel_iov *iov)
{
	struct intel_gt *gt = iov_to_gt(iov);
	struct i915_ggtt *ggtt = gt->ggtt;
	s64 ggtt_shift;

	mutex_lock(&ggtt->vm.mutex);

	ggtt_shift = vf_get_post_migration_ggtt_shift(iov);
	i915_ggtt_shift_nodes(ggtt, iov->vf.ggtt_balloon, ggtt_shift);

	mutex_unlock(&ggtt->vm.mutex);
}
