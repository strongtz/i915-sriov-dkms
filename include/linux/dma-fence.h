#include_next <linux/dma-fence.h>
#ifndef __BACKPORT_DMA_FENCE_H__
#define __BACKPORT_DMA_FENCE_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,17,0) // unsafe before 6.17
static inline const char *dma_fence_driver_name(struct dma_fence *fence)
{
	return fence->ops->get_driver_name(fence);
}

static inline const char *dma_fence_timeline_name(struct dma_fence *fence)
{
	return fence->ops->get_timeline_name(fence);
}
#endif

#endif
