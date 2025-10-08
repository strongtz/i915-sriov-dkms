#include_next <drm/ttm/ttm_tt.h>

#ifndef __BACKPORT_DRM_TTM_TT_H__
#define __BACKPORT_DRM_TTM_TT_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
/**
 * ttm_tt_is_swapped() - Whether the ttm_tt is swapped out or backed up
 * @tt: The struct ttm_tt.
 *
 * Return: true if swapped or backed up, false otherwise.
 */
static inline bool ttm_tt_is_swapped(const struct ttm_tt *tt)
{
	return tt->page_flags & (TTM_TT_FLAG_SWAPPED);
}
#endif

#endif
