/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_MODULE_H_
#define _XE_MODULE_H_

#include <linux/types.h>

/* Module modprobe variables */
struct xe_modparam {
	bool force_execlist;
	bool probe_display;
	u32 force_vram_bar_size;
	int guc_log_level;
	char *guc_firmware_path;
	char *huc_firmware_path;
	char *gsc_firmware_path;
	char *force_probe;
#ifdef CONFIG_PCI_IOV
	unsigned int max_vfs;
#endif
	int wedged_mode;
	u32 svm_notifier_size;
};

extern struct xe_modparam xe_modparam;

#endif

