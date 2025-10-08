#include_next <linux/bits.h>

#ifndef __BACKPORT_LINUX_BITS_H__
#define __BACKPORT_LINUX_BITS_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
#include <linux/overflow.h>
/*
 * Fixed-type variants of BIT(), with additional checks like GENMASK_TYPE(). The
 * following examples generate compiler warnings due to -Wshift-count-overflow:
 *
 * - BIT_U8(8)
 * - BIT_U32(-1)
 * - BIT_U32(40)
 */
#define BIT_INPUT_CHECK(type, nr) \
	BUILD_BUG_ON_ZERO(const_true((nr) >= BITS_PER_TYPE(type)))

#define BIT_TYPE(type, nr) ((type)(BIT_INPUT_CHECK(type, nr) + BIT_ULL(nr)))

#define BIT_U8(nr)	BIT_TYPE(u8, nr)
#define BIT_U16(nr)	BIT_TYPE(u16, nr)
#define BIT_U32(nr)	BIT_TYPE(u32, nr)
#define BIT_U64(nr)	BIT_TYPE(u64, nr)

/*
 * Generate a mask for the specified type @t. Additional checks are made to
 * guarantee the value returned fits in that type, relying on
 * -Wshift-count-overflow compiler check to detect incompatible arguments.
 * For example, all these create build errors or warnings:
 *
 * - GENMASK(15, 20): wrong argument order
 * - GENMASK(72, 15): doesn't fit unsigned long
 * - GENMASK_U32(33, 15): doesn't fit in a u32
 */
#define GENMASK_TYPE(t, h, l)					\
	((t)(GENMASK_INPUT_CHECK(h, l) +			\
	     (type_max(t) << (l) &				\
	      type_max(t) >> (BITS_PER_TYPE(t) - 1 - (h)))))

#define GENMASK_U8(h, l)	GENMASK_TYPE(u8, h, l)
#define GENMASK_U16(h, l)	GENMASK_TYPE(u16, h, l)
#define GENMASK_U32(h, l)	GENMASK_TYPE(u32, h, l)
#define GENMASK_U64(h, l)	GENMASK_TYPE(u64, h, l)
#endif

#endif
