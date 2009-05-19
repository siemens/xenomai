/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _XENO_ASM_GENERIC_BITS_TIMECONV_H
#define _XENO_ASM_GENERIC_BITS_TIMECONV_H

#include <asm/xenomai/arith.h>

static unsigned long long cpufreq;

#ifdef XNARCH_HAVE_LLMULSHFT
static unsigned int tsc_scale, tsc_shift;
#endif

#ifdef XNARCH_HAVE_LLMULSHFT
long long xnarch_tsc_to_ns(long long ticks)
{
	return xnarch_llmulshft(ticks, tsc_scale, tsc_shift);
}
long long xnarch_tsc_to_ns_rounded(long long ticks)
{
	unsigned int shift = tsc_shift - 1;
	return (xnarch_llmulshft(ticks, tsc_scale, shift) + 1) / 2;
}
#else  /* !XNARCH_HAVE_LLMULSHFT */
long long xnarch_tsc_to_ns(long long ticks)
{
	return xnarch_llimd(ticks, 1000000000, cpufreq);
}
long long xnarch_tsc_to_ns_rounded(long long ticks)
{
	return (xnarch_llimd(ticks, 1000000000, cpufreq/2) + 1) / 2;
}
#endif /* !XNARCH_HAVE_LLMULSHFT */

long long xnarch_ns_to_tsc(long long ns)
{
	return xnarch_llimd(ns, cpufreq, 1000000000);
}

static inline void xnarch_init_timeconv(unsigned long long freq)
{
	cpufreq = freq;
#ifdef XNARCH_HAVE_LLMULSHFT
	xnarch_init_llmulshft(1000000000, freq, &tsc_scale, &tsc_shift);
#endif
}

#ifdef __KERNEL__
EXPORT_SYMBOL(xnarch_tsc_to_ns);
EXPORT_SYMBOL(xnarch_ns_to_tsc);
#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_GENERIC_BITS_TIMECONV_H */
