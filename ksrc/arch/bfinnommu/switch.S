/*
 * Copyright (C) 2005 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/linkage.h>

/*
 * Switch context routine for kernel-based Xenomai threads.
 * Simple and easy: we basically see kernel threads as co-routines.
 *
 * rthal_kcontext_switch(unsigned long *out_kspp, unsigned long *in_kspp)
 */
ENTRY(rthal_kcontext_switch)

	p0 = r0;
	p1 = r1;
	[--sp] = rets;
	[--sp] = fp;
	[--sp] = r0;

	/* Exchange stacks. */
	[p0] = sp;
	sp = [p1];

	r0 = [sp++];
	fp = [sp++];
	rets = [sp++];
	rts;

/*
 * Switch context routine for user-space Xenomai shadow threads.
 * We protect all caller-saved registers across switches.
 * 
 * rthal_ucontext_switch(struct task_struct *prev, struct task_struct *next)
 */
ENTRY(rthal_ucontext_switch)

	[--sp] = rets;
	[--sp] = r4;
	[--sp] = r5;
  	[--sp] = r6;
  	[--sp] = r7;
	[--sp] = p3;
	[--sp] = p4;
	[--sp] = p5;
	call resume;
	p5 = [sp++];
	p4 = [sp++];
	p3 = [sp++];
	r7 = [sp++];
	r6 = [sp++];
	r5 = [sp++];
	r4 = [sp++];
	rets = [sp++];
	rts
