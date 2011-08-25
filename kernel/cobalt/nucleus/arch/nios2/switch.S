/*
 * Copyright (C) 2009 Philippe Gerum.
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
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/entry.h>

.text
.set noat
.set nobreak

.macro GET_THREAD_INFO reg 
.if THREAD_SIZE & 0xffff0000
	andhi	\reg, sp, %hi(~(THREAD_SIZE-1))
.else
	addi	\reg, r0, %lo(~(THREAD_SIZE-1))
	and	\reg, \reg, sp
.endif
.endm

/*
 * Switch context routine for Xenomai threads.
 * Kernel-based threads are basically seen as co-routines.
 * The context is saved/restored to/from regular thread_structs,
 * so that hybrid scheduling between kernel and user-space Xenomai
 * threads is possible.
 *
 * void rthal_thread_switch(struct thread_struct *prev,
 *                          struct thread_struct *next,
 *                          int kernp)
 */
ENTRY(rthal_thread_switch)
	rdctl	r7,status			/* save thread status reg */
	stw	r7,THREAD_KPSR(r4)	
	SAVE_SWITCH_STACK
	stw	sp,THREAD_KSP(r4)		/* save kernel stack pointer */
	ldw	sp,THREAD_KSP(r5)		/* restore new thread stack */
	bne	r6,r0,no_curr_update		/* switch to kernel Xenomai thread */
	movia	r24,_current_thread		/* save thread */
	GET_THREAD_INFO r1
	stw	r1,0(r24)
no_curr_update:
	RESTORE_SWITCH_STACK
	ldw	r7,THREAD_KPSR(r5)		/* restore thread status reg */
	wrctl	status,r7
	ret

ENTRY(rthal_thread_trampoline)
	ldw	r4,0(sp)
	ldw	ra,4(sp)
	addi	sp,sp,8
	ret
