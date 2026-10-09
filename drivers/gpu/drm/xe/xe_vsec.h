/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright © 2024 Intel Corporation */

#ifndef _XE_VSEC_H_
#define _XE_VSEC_H_

#include <linux/types.h>

#ifdef IDB_XE_PMT_TELEM_READ_USE_KERNEL_DEV
struct device;
#else
struct pci_dev;
#endif
struct xe_device;

void xe_vsec_init(struct xe_device *xe);
#ifdef IDB_XE_PMT_TELEM_READ_USE_KERNEL_DEV
int xe_pmt_telem_read(struct device *dev, u32 guid, u64 *data, loff_t user_offset, u32 count);
#else
int xe_pmt_telem_read(struct pci_dev *pdev, u32 guid, u64 *data, loff_t user_offset, u32 count);
#endif

#endif
