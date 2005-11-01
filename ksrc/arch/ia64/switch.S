/*
 * Copied from ia64/kernel/entry.S
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999, 2002-2003
 *	Asit Mallick <Asit.K.Mallick@intel.com>
 * 	Don Dugger <Don.Dugger@intel.com>
 *	Suresh Siddha <suresh.b.siddha@intel.com>
 *	Fenghua Yu <fenghua.yu@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 */
/*
 * ia64_switch_to now places correct virtual mapping in in TR2 for
 * kernel stack. This allows us to handle interrupts without changing
 * to physical mode.
 *
 * Jonathan Nicklin	<nicklin@missioncriticallinux.com>
 * Patrick O'Rourke	<orourke@missioncriticallinux.com>
 * 11/07/2000
 /
/*
 * Global (preserved) predicate usage on syscall entry/exit path:
 *
 *	pKStk:		See entry.h.
 *	pUStk:		See entry.h.
 *	pSys:		See entry.h.
 *	pNonSys:	!pSys
 */

#include <linux/config.h>
#include <entry.h>

#include <asm/asmmacro.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/kregs.h>
#include <asm/offsets.h>
#include <asm/pgtable.h>
#include <asm/percpu.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>    


/* int rthal_prepare_stack(unsigned long stackbase) */
	
GLOBAL_ENTRY(rthal_prepare_stack)
	.prologue
	alloc r16=ar.pfs,1,8,0,8
	.rotr v[2]			// declare our 2 aliases for rotating
	adds sp=-16,sp
	DO_SAVE_SWITCH_STACK
	.body
	;;
	mov r18=in0		        // r18 = stackbase (copy destination)
	mov r19=IA64_SWITCH_STACK_SIZE-8
	;;
	shr r19=r19,3			// r19=(8-bytes word count)-1
	adds r18=-8,r18			// simulate initial pre-decrementation of dest index
	adds r20=IA64_SWITCH_STACK_SIZE+8,sp // r20=((struct switch_stack *)sp + 1) + 8-bytes padding
	;; 
	mov ar.lc=r19			// loop count in repeat/until fashion
	mov ar.ec=2			// two pipeline stages are active when copying
	mov pr.rot=1<<16		// use p16(=1) and p17(=0) for predicates
	;;
copy_domain_stack:
(p16)	ld8 v[0]=[r20],-8
(p17)	st8 [r18]=v[1],-8
	br.ctop.dptk.few copy_domain_stack
	;;
	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE+16,sp
	mov ar.pfs=r16
	br.ret.sptk.many rp
END(rthal_prepare_stack)


/* void rthal_switch_context(xnarchtcb *out_tcb	--> prev nucleus thread
	, xnarchtcb *in_tcb)			--> next nucleus thread */
		
GLOBAL_ENTRY(rthal_switch_context)
	.prologue
	alloc r16=ar.pfs,2,0,0,0         // in0: prev in1: next
	DO_SAVE_SWITCH_STACK
	.body

	ld8 r22=[in0]
	ld8 r21=[in1]
	;;	
	st8 [r22]=sp			// save kernel stack pointer of old task
	ld8 sp=[r21]			// load kernel stack pointer of new task
	;;
	DO_LOAD_SWITCH_STACK

#ifdef CONFIG_SMP
	sync.i				// ensure "fc"s done by this CPU are visible on other CPUs
#endif
	br.ret.sptk.many rp		// boogie on out in new context
END(rthal_switch_context)
