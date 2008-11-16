/**
 *   @ingroup hal
 *   @file
 *
 *   Arithmetic/conversion routines for x86_64.
 *
 *   Copyright &copy; 2007 Jan Kiszka.
 *
 *   Xenomai is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   Xenomai is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Xenomai; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */
#ifndef _XENO_ASM_X86_ARITH_64_H
#define _XENO_ASM_X86_ARITH_64_H
#define _XENO_ASM_X86_ARITH_H

static inline __attribute__((__const__)) long long
__rthal_x86_64_llimd (long long op, unsigned m, unsigned d)
{
	long long result;

	__asm__ (
		"imul %[m]\n\t"
		"idiv %[d]\n\t"
		: "=a" (result)
		: "a" (op), [m] "r" ((unsigned long long)m),
		  [d] "r" ((unsigned long long)d)
		: "rdx");

	return result;
}
#define rthal_llimd(ll,m,d) __rthal_x86_64_llimd((ll),(m),(d))

static inline __attribute__((__const__)) long long
__rthal_x86_64_llmulshft(long long op, unsigned m, unsigned s)
{
	long long result;

	__asm__ (
		"imul %[m]\n\t"
		"shrd %%cl,%%rdx,%%rax\n\t"
		: "=a,a" (result)
		: "a,a" (op), [m] "m,r" ((unsigned long long)m),
		  "c,c" (s)
		: "rdx");

	return result;
}
#define rthal_llmulshft(op, m, s) __rthal_x86_64_llmulshft((op), (m), (s))

#define XNARCH_WANT_NODIV_MULDIV

static inline __attribute__((__const__)) unsigned long long
__rthal_x86_64_nodiv_ullimd(unsigned long long op, unsigned long long frac,
			    unsigned integ)
{
	unsigned long long rh, rl;
	__asm__ ("mulq %[op]\n\t":
		 "=d"(rh), "=a"(rl):
		 "1"(frac), [op]"r"(op));
	__asm__ ("addq %[rl], %[t]\n\t"
		 "adcq $0, %[rh]\n\t":
		 [rh]"+r"(rh), [rl]"+r"(rl):
		 [t]"r"((rl & (1ULL << 31)) << 1):
		 "cc");
	return rh + op * integ;
}

#define rthal_nodiv_ullimd(op, frac, integ) \
	__rthal_x86_64_nodiv_ullimd((op), (frac), (integ))

#include <asm-generic/xenomai/arith.h>

#endif /* _XENO_ASM_X86_ARITH_64_H */
