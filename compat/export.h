/*
 * Copyright (c) 2025
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __INTEL_SRIOV_COMPAT_EXPORT_H__
#define __INTEL_SRIOV_COMPAT_EXPORT_H__
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 13)
#define DEFAULT_SYMBOL_NAMESPACE INTEL_SRIOV_COMPAT
#else
#define DEFAULT_SYMBOL_NAMESPACE "INTEL_SRIOV_COMPAT"
#endif
#endif
