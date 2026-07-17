#include_next <linux/dma-fence.h>
#ifndef __BACKPORT_DMA_FENCE_H__
#define __BACKPORT_DMA_FENCE_H__


#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
#define dma_fence_check_and_signal LINUX_BACKPORT(dma_fence_check_and_signal)
bool dma_fence_check_and_signal(struct dma_fence *fence);
#define dma_fence_check_and_signal_locked LINUX_BACKPORT(dma_fence_check_and_signal_locked)
bool dma_fence_check_and_signal_locked(struct dma_fence *fence);

/*
 * dma_fence_test_signaled_flag - Only check whether a fence is signaled yet.
 * @fence: the fence to check
 *
 * This function just checks whether @fence is signaled, without interacting
 * with the fence in any way. The user must, therefore, ensure through other
 * means that fences get signaled eventually.
 *
 * This function uses test_bit(), which is thread-safe. Naturally, this function
 * should be used opportunistically; a fence could get signaled at any moment
 * after the check is done.
 *
 * Return: true if signaled, false otherwise.
 */
static inline bool
dma_fence_test_signaled_flag(struct dma_fence *fence)
{
	return test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
/*
 * Linux 7.1: struct dma_fence.lock (spinlock_t *) was replaced by a union
 * extern_lock/inline_lock; access is now only via dma_fence_spinlock().
 * Backport for older kernels so the vendored code can use the accessor
 * uniformly (on 7.1+ the kernel header provides the real static inline).
 */
static inline spinlock_t *dma_fence_spinlock(struct dma_fence *fence)
{
	return fence->lock;
}
#endif
#endif
