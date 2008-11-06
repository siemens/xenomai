#ifndef _XENO_ASM_POWERPC_ARITH_H
#define _XENO_ASM_POWERPC_ARITH_H

#define XNARCH_WANT_NODIV_MULDIV

#ifdef __powerpc64__

#define rthal_nodiv_ullimd(op, frac, integ) \
	rthal_powerpc64_nodiv_ullimd((op), (frac), (integ))

static inline __attribute__((__const__)) unsigned long long
rthal_powerpc64_nodiv_ullimd(const unsigned long long op,
			     const unsigned long long frac,
			     const unsigned rhs_integ)
{
	unsigned long h, l, m;

	__asm__("mulhdu   %0, %3, %4\n\t"			\
		"mulld    %1, %3, %4\n\t"			\
		"rlwinm   %2, %1, 0, 0, 0\n\t"			\
		"sldi     %2, %2, 1\n\t"			\
		"addc     %1, %1, %2\n\t"			\
		"addze    %0, %0\n\t"				\
		"adde     %0, %0, %5\n\t"			\
		: "=&r"(h), "=&r"(l), "=&r"(m)			\
		: "r"(op), "r"(frac), "r"(rhs_integ) : "cc");	\
	  
	return h;
}

#else /* !__powerpc64__ */

#define __rthal_add96and64(l0, l1, l2, s0, s1)		\
	do {						\
		__asm__ ("addc %2, %2, %4\n\t"		\
			 "adde %1, %1, %3\n\t"		\
			 "addze %0, %0\n\t"		\
			 : "+r"(l0), "+r"(l1), "+r"(l2)	\
			 : "r"(s0), "r"(s1) : "cc");	\
	} while (0)

#endif /* !__powerpc64__ */

#include <asm-generic/xenomai/arith.h>

#endif /* _XENO_ASM_POWERPC_ARITH_H */
