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
#include <asm/blackfin.h>
#include <asm/entry.h>
#include <asm/asm-offsets.h>

/*
 * Switch context routine for kernel-based Xenomai threads.
 * Simple and easy: we basically see kernel threads as co-routines.
 *
 * void rthal_kcontext_switch(unsigned long *out_kspp, unsigned long *in_kspp)
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
 * void rthal_ucontext_switch(struct task_struct *prev, struct task_struct *next)
 */
ENTRY(rthal_ucontext_switch)

	[--sp] = rets;
	[--sp] = ( R7:4, P5:3 );
	call resume;
	( R7:4, P5:3 ) = [sp++];
	rets = [sp++];
	rts

/*
 * Check for a pending syscall event. On this arch, syscalls start as
 * exceptions, which are deferred to low priority interrupts for
 * processing them. Returns true if the syscall has been deferred.
 * 
 * int rthal_defer_switch_p(void)
 */

ENTRY(rthal_defer_switch_p)

#ifdef CONFIG_XENO_OPT_PERVASIVE
	p2.l = lo(ILAT);
        p2.h = hi(ILAT);
        r0 = [p2];
	r1 = EVT_IVG15 (z);
        r0 = r0 & r1;
	cc = r0 == 0;
	if !cc jump 1f; 
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	p2.l = lo(IPEND);
        p2.h = hi(IPEND);
	csync;
        r2 = [p2];
        r1 = 1;
        r1 = r2 - r1;
        r0 = r2 & r1;
1:
	rts

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY

ENTRY(rthal_nmi_handler)
	SAVE_ALL_SYS
	r0 = sp;
	SP += -12;	
	call rthal_nmi_tick
	SP += 12;	
	RESTORE_ALL_SYS
	rtn; 
		
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY_MAX */

ENTRY(rthal_get_ipend)

	p2.l = lo(IPEND);
        p2.h = hi(IPEND);
	csync;
        r0 = [p2];
	rts
