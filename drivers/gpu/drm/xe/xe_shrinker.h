/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,15,0)
#ifndef _XE_SHRINKER_H_
#define _XE_SHRINKER_H_

struct xe_shrinker;
struct xe_device;

void xe_shrinker_mod_pages(struct xe_shrinker *shrinker, long shrinkable, long purgeable);

int xe_shrinker_create(struct xe_device *xe);

#endif
#endif
