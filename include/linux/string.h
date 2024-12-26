#include <linux/version.h>
#include_next <linux/string.h>

#ifndef _BACKPORT_LINUX_STRING_H
#define _BACKPORT_LINUX_STRING_H
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
void *kmemdup_array(const void *src, size_t element_size, size_t count, gfp_t gfp);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
/**
 * mem_is_zero - Check if an area of memory is all 0's.
 * @s: The memory area
 * @n: The size of the area
 *
 * Return: True if the area of memory is all 0's.
 */
static inline bool mem_is_zero(const void *s, size_t n)
{
	return !memchr_inv(s, 0, n);
}
#endif
#endif