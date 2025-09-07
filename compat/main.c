/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>

static int __init backport_init(void)
{
	pr_info("intel_sriov_compat: MODULE INIT\n");
	return 0;
}

static void __exit backport_exit(void)
{
  pr_info("intel_sriov_compat: MODULE EXIT\n");
}

module_init(backport_init);
module_exit(backport_exit);
MODULE_AUTHOR("Contributors of i915-sriov-dkms");
MODULE_DESCRIPTION("compatibility module for older kernels");

MODULE_INFO(url, "https://github.com/strongtz/i915-sriov-dkms");

#ifdef DKMS_MODULE_VERSION
MODULE_VERSION(DKMS_MODULE_VERSION);
#endif
#ifdef DKMS_MODULE_ORIGIN_KERNEL
MODULE_INFO(origin_kernel, DKMS_MODULE_ORIGIN_KERNEL);
#endif

MODULE_LICENSE("GPL and additional rights");
