/*
 * Copyright (C) 2009 Wolfgang Mauerer <wolfgang.mauerer@siemens.com>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#ifndef _COBALT_ASM_GENERIC_SEQLOCK_H
#define _COBALT_ASM_GENERIC_SEQLOCK_H

/* Originally from the linux kernel, adapted for userland and Xenomai */

#ifdef __KERNEL__
#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/xenomai/wrappers.h>
#else
#include <asm-generic/xenomai/atomic.h>
#endif

typedef struct xnseqcount {
	unsigned int sequence;
} xnseqcount_t;

#define XNSEQCNT_ZERO { 0 }
#define xnseqcount_init(x) do { *(x) = (xnseqcount_t) SEQCNT_ZERO; } while (0)

/* Start of read using pointer to a sequence counter only.  */
static inline unsigned xnread_seqcount_begin(const xnseqcount_t *s)
{
	unsigned ret;

repeat:
	ret = s->sequence;
	smp_rmb();
	if (ret & 1) {
		cpu_relax();
		goto repeat;
	}
	return ret;
}

/*
 * Test if reader processed invalid data because sequence number has changed.
 */
static inline int xnread_seqcount_retry(const xnseqcount_t *s, unsigned start)
{
	smp_rmb();

	return s->sequence != start;
}

/*
 * The sequence counter only protects readers from concurrent writers.
 * Writers must use their own locking.
 */
static inline void xnwrite_seqcount_begin(xnseqcount_t *s)
{
	s->sequence++;
	smp_wmb();
}

static inline void xnwrite_seqcount_end(xnseqcount_t *s)
{
	smp_wmb();
	s->sequence++;
}

#endif /* !_COBALT_ASM_GENERIC_SEQLOCK_H */
