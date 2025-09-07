#include_next <linux/compiler.h>
#ifndef __BACKPORT_LINUX_COMPILER_H__
#define __BACKPORT_LINUX_COMPILER_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,14,0)
/*
 * Similar to statically_true() but produces a constant expression
 *
 * To be used in conjunction with macros, such as BUILD_BUG_ON_ZERO(),
 * which require their input to be a constant expression and for which
 * statically_true() would otherwise fail.
 *
 * This is a trade-off: const_true() requires all its operands to be
 * compile time constants. Else, it would always returns false even on
 * the most trivial cases like:
 *
 *   true || non_const_var
 *
 * On the opposite, statically_true() is able to fold more complex
 * tautologies and will return true on expressions such as:
 *
 *   !(non_const_var * 8 % 4)
 *
 * For the general case, statically_true() is better.
 */
#define const_true(x) __builtin_choose_expr(__is_constexpr(x), x, false)
#endif

#endif
