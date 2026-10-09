#include_next <linux/bitfield.h>

#ifndef __BACKPORT_LINUX_BITFIELD_H__
#define __BACKPORT_LINUX_BITFIELD_H__

#ifndef FIELD_MODIFY
/**
 * FIELD_MODIFY() - modify a bitfield element
 * @_mask: shifted mask defining the field's length and position
 * @_reg_p: pointer to the memory that should be updated
 * @_val: value to store in the bitfield
 *
 * FIELD_MODIFY() modifies the set of bits in @_reg_p specified by @_mask,
 * by replacing them with the bitfield value passed in as @_val.
 */
#define FIELD_MODIFY(_mask, _reg_p, _val)						\
	({										\
		typecheck_pointer(_reg_p);						\
		__BF_FIELD_CHECK(_mask, *(_reg_p), _val, "FIELD_MODIFY: ");		\
		*(_reg_p) &= ~(_mask);							\
		*(_reg_p) |= (((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask));	\
	})

#endif /* FIELD_MODIFY */

#endif /* __BACKPORT_LINUX_BITFIELD_H__ */