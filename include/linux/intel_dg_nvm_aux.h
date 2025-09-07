#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,17,0)
#include_next <linux/intel_dg_nvm_aux.h>
#endif

#ifndef __BACKPORT_INTEL_DG_NVM_AUX_H__
#define __BACKPORT_INTEL_DG_NVM_AUX_H__
/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019-2025, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_DG_NVM_AUX_H__
#define __INTEL_DG_NVM_AUX_H__

#include <linux/auxiliary_bus.h>
#include <linux/container_of.h>
#include <linux/ioport.h>
#include <linux/types.h>

#define INTEL_DG_NVM_REGIONS 13

struct intel_dg_nvm_region {
	const char *name;
};

struct intel_dg_nvm_dev {
	struct auxiliary_device aux_dev;
	bool writable_override;
	bool non_posted_erase;
	struct resource bar;
	struct resource bar2;
	const struct intel_dg_nvm_region *regions;
};

#define auxiliary_dev_to_intel_dg_nvm_dev(auxiliary_dev) \
	container_of(auxiliary_dev, struct intel_dg_nvm_dev, aux_dev)

#endif /* __INTEL_DG_NVM_AUX_H__ */
#endif /* __BACKPORT_INTEL_DG_NVM_AUX_H__ */
