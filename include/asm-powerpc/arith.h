#ifndef _XENO_ASM_POWERPC_ARITH_H
#define _XENO_ASM_POWERPC_ARITH_H

#define XNARCH_WANT_NODIV_MULDIV

#define __rthal_add96and64(l0, l1, l2, s0, s1)		\
	do {						\
		__asm__ ("addc %2, %2, %4\n\t"		\
			 "adde %1, %1, %3\n\t"		\
			 "addze %0, %0\n\t"		\
			 : "+r"(l0), "+r"(l1), "+r"(l2)	\
			 : "r"(s0), "r"(s1) : "cc");	\
	} while (0)

#include <asm-generic/xenomai/arith.h>

#endif /* _XENO_ASM_POWERPC_ARITH_H */
