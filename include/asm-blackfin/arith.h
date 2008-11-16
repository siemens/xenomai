#ifndef _XENO_ASM_BLACKFIN_ARITH_H
#define _XENO_ASM_BLACKFIN_ARITH_H

#define XNARCH_WANT_NODIV_MULDIV

#define __rthal_add96and64(l0, l1, l2, s0, s1)		\
	do {						\
	  unsigned long cl, ch;				\
	  __asm__ ("%2 = %2 + %6\n\t"			\
		   "CC = AC0\n\t"			\
		   "%3 = CC\n\t"			\
		   "%1 = %1 + %5\n\t"			\
		   "CC = AC0\n\t"			\
		   "%4 = CC\n\t"			\
		   "%1 = %1 + %3\n\t"			\
		   "CC = AC0\n\t"			\
		   "%3 = CC\n\t"			\
		   "%4 = %4 + %3\n\t"			\
		   "%0 = %0 + %4\n\t"			\
		   : "+d"(l0), "+d"(l1), "+d"(l2), "=&d" (cl), "=&d" (ch) \
		   : "d"(s0), "d"(s1) : "cc");				\
	} while (0)

#include <asm-generic/xenomai/arith.h>

#endif /* _XENO_ASM_BLACKFIN_ARITH_H */
