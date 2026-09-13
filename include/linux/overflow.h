#include_next <linux/overflow.h>

#ifndef __BACKPORT_LINUX_OVERFLOW_H__
#define __BACKPORT_LINUX_OVERFLOW_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
/**
 * typeof_flex_counter() - Return the type of the counter variable of a given
 *                         flexible array member annotated by __counted_by().
 * @FAM: Instance of flexible array member within a given struct.
 *
 * Returns: "size_t" if no annotation exists.
 */
#define typeof_flex_counter(FAM)				\
	typeof(_Generic(__flex_counter(FAM),			\
			void *: (size_t)0,			\
			default: *__flex_counter(FAM)))

/**
 * overflows_flex_counter_type() - Check if the counter associated with the
 *				   given flexible array member can represent
 *				   a value.
 * @TYPE: Type of the struct that contains the @FAM.
 * @FAM: Member name of the FAM within @TYPE.
 * @COUNT: Value to check against the __counted_by annotated @FAM's counter.
 *
 * Returns: true if @COUNT can be represented in the @FAM's counter. When
 * @FAM is not annotated with __counted_by(), always returns true.
 */
#define overflows_flex_counter_type(TYPE, FAM, COUNT)		\
	(overflows_type(COUNT, typeof_flex_counter(((TYPE *)NULL)->FAM)))

/**
 * __set_flex_counter() - Set the counter associated with the given flexible
 *                        array member that has been annoated by __counted_by().
 * @FAM: Instance of flexible array member within a given struct.
 * @COUNT: Value to store to the __counted_by annotated @FAM_PTR's counter.
 *
 * This is a no-op if no annotation exists. Count needs to be checked with
 * overflows_flex_counter_type() before using this function.
 */
#define __set_flex_counter(FAM, COUNT)				\
({								\
	*_Generic(__flex_counter(FAM),				\
		  void *:  &(size_t){ 0 },			\
		  default: __flex_counter(FAM)) = (COUNT);	\
})
#endif

#endif
