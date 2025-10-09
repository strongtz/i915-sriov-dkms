#include_next <drm/drm_print.h>
#ifndef __BACKPORT_DRM_PRINT_H__
#define __BACKPORT_DRM_PRINT_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
#define __drm_printfn_line_dummy LINUX_BACKPORT(__drm_printfn_line_dummy)
void __drm_printfn_line_dummy(struct drm_printer *p, struct va_format *vaf);
static inline struct drm_printer drm_line_printer(struct drm_printer *p,
						  const char *prefix,
						  unsigned int series)
{
	struct drm_printer lp = {
		.printfn = __drm_printfn_line_dummy,
		.arg = p,
		.prefix = prefix,
	};
	return lp;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 14, 0)
#include <drm/drm_device.h>
#define drm_print_hex_dump LINUX_BACKPORT(drm_print_hex_dump)
void drm_print_hex_dump(struct drm_printer *p, const char *prefix,
			const u8 *buf, size_t len);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
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
#define drm_coredump_printer_is_full LINUX_BACKPORT(drm_coredump_printer_is_full)
static inline bool drm_coredump_printer_is_full(struct drm_printer *p)
{
	struct drm_print_iterator *iterator = p->arg;

	if (p->printfn != __drm_printfn_coredump)
		return true;

	return !iterator->remain;
}
#endif

#endif
