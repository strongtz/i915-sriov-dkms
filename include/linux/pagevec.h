#ifndef __BACKPORT_LINUX_PAGEVEC_H__
#define __BACKPORT_LINUX_PAGEVEC_H__

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
/* Kernels < 7.1 still ship <linux/pagevec.h>. */
#include_next <linux/pagevec.h>
#endif

/*
 * Linux 7.1 removed <linux/pagevec.h> (the pagevec -> folio_batch
 * conversion is complete). The vendored i915 tree still includes the
 * header but no longer uses any pagevec symbols (verified: no
 * `struct pagevec`/`pagevec_*` usage), so an empty shim suffices on 7.1+.
 */

#endif /* __BACKPORT_LINUX_PAGEVEC_H__ */
