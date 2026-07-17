/*
 * Copyright (c) 2026
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/dma-fence.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
/**
 * dma_fence_check_and_signal_locked - signal the fence if it's not yet signaled
 * @fence: the fence to check and signal
 *
 * Checks whether a fence was signaled and signals it if it was not yet signaled.
 *
 * Unlike dma_fence_check_and_signal(), this function must be called with
 * &struct dma_fence.lock being held.
 *
 * Return: true if fence has been signaled already, false otherwise.
 */
bool dma_fence_check_and_signal_locked(struct dma_fence *fence)
{
	bool ret;

	ret = dma_fence_test_signaled_flag(fence);
	dma_fence_signal_locked(fence);

	return ret;
}
EXPORT_SYMBOL(dma_fence_check_and_signal_locked);

/**
 * dma_fence_check_and_signal - signal the fence if it's not yet signaled
 * @fence: the fence to check and signal
 *
 * Checks whether a fence was signaled and signals it if it was not yet signaled.
 * All this is done in a race-free manner.
 *
 * Return: true if fence has been signaled already, false otherwise.
 */
bool dma_fence_check_and_signal(struct dma_fence *fence)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(dma_fence_spinlock(fence), flags);
	ret = dma_fence_check_and_signal_locked(fence);
	spin_unlock_irqrestore(dma_fence_spinlock(fence), flags);

	return ret;
}
EXPORT_SYMBOL(dma_fence_check_and_signal);
#endif
