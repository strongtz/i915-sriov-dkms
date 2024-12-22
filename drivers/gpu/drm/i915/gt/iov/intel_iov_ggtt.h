/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_IOV_GGTT_H__
#define __INTEL_IOV_GGTT_H__

#include "gt/intel_gtt.h"
#include "abi/iov_actions_mmio_abi.h"

struct intel_iov;

int intel_iov_ggtt_pf_update_vf_ptes(struct intel_iov *iov, u32 vfid, u32 pte_offset, u8 mode,
				     u16 num_copies, gen8_pte_t *ptes, u16 count);
void intel_iov_ggtt_vf_init_early(struct intel_iov *iov);
void intel_iov_ggtt_vf_release(struct intel_iov *iov);

void intel_iov_ggtt_vf_update_pte(struct intel_iov *iov, u32 offset, gen8_pte_t pte);
void intel_iov_ggtt_vf_flush_ptes(struct intel_iov *iov);

int intel_iov_ggtt_shadow_init(struct intel_iov *iov);
void intel_iov_ggtt_shadow_fini(struct intel_iov *iov);

int intel_iov_ggtt_shadow_vf_alloc(struct intel_iov *iov, unsigned int vfid,
				   struct drm_mm_node *ggtt_region);
void intel_iov_ggtt_shadow_vf_free(struct intel_iov *iov, unsigned int vfid);

void intel_iov_ggtt_shadow_set_pte(struct intel_iov *iov, unsigned int vfid, u64 pte_offset,
				   gen8_pte_t pte);
gen8_pte_t intel_iov_ggtt_shadow_get_pte(struct intel_iov *iov, unsigned int vfid, u64 pte_offset);

int intel_iov_ggtt_shadow_save(struct intel_iov *iov, unsigned int vfid, void *buf, size_t size,
			       unsigned int flags);
int intel_iov_ggtt_shadow_restore(struct intel_iov *iov, unsigned int vfid, const void *buf,
				  size_t size, unsigned int flags);

#endif /* __INTEL_IOV_GGTT_H__ */
