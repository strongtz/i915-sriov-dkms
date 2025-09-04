#include_next <drm/drm_print.h>
#ifndef __BACKPORT_DRM_PRINT_H__
#define __BACKPORT_DRM_PRINT_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,16,0)
/**
 * drm_coredump_printer_is_full() - DRM coredump printer output is full
 * @p: DRM coredump printer
 *
 * DRM printer output is full, useful to short circuit coredump printing once
 * printer is full.
 *
 * RETURNS:
 * True if DRM coredump printer output buffer is full, False otherwise
 */
static inline bool drm_coredump_printer_is_full(struct drm_printer *p)
{
	struct drm_print_iterator *iterator = p->arg;

	if (p->printfn != __drm_printfn_coredump)
		return true;

	return !iterator->remain;
}
#endif

#endif
