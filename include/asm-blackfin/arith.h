#ifndef _XENO_ASM_BLACKFIN_ARITH_H
#define _XENO_ASM_BLACKFIN_ARITH_H

#define XNARCH_WANT_NODIV_MULDIV

/*
 * Reading the 64bit part indirectly may seem a bit twisted, but we
 * don't have many Dregs on the Blackfin, and __rthal_mul64by64_high()
 * grabs most of them. Still, nodiv_ullimd performs 5x faster than
 * ullimd on this arch.
 */
#define __rthal_add96and64(l0, l1, l2, s0, s1)		\
	do {						\
	  unsigned long cl, ch, _s0 = (s0), _s1 = (s1);	\
	  __asm__ ("%3 = [%6]\n\t"			\
		   "%2 = %2 + %3\n\t"			\
		   "CC = AC0\n\t"			\
		   "%3 = CC\n\t"			\
		   "%4 = [%5]\n\t"			\
		   "%1 = %1 + %4\n\t"			\
		   "CC = AC0\n\t"			\
		   "%4 = CC\n\t"			\
		   "%1 = %1 + %3\n\t"			\
		   "CC = AC0\n\t"			\
		   "%3 = CC\n\t"			\
		   "%4 = %4 + %3\n\t"			\
		   "%0 = %0 + %4\n\t"			\
		   : "+r"(l0), "+r"(l1), "+r"(l2), "=&r" (cl), "=&r" (ch) \
		   : "a"(&_s0), "a"(&_s1) : "cc");			\
	} while (0)

#include <asm-generic/xenomai/arith.h>

#endif /* _XENO_ASM_BLACKFIN_ARITH_H */
