/*
 * Copyright (C) 2004-2009 Philippe Gerum.
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

#include <linux/version.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include "ppc_defs.h"
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
#include <asm/offsets.h>
#else
#include <asm/asm-offsets.h>
#endif

#ifdef CONFIG_PPC601_SYNC_FIX
#define SYNC				\
BEGIN_FTR_SECTION			\
	sync;				\
	isync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#else /* !CONFIG_PPC601_SYNC_FIX */
#define	SYNC
#endif /* CONFIG_PPC601_SYNC_FIX */

#ifdef CONFIG_PPC64
#ifndef MTMSRD
#define MTMSRD(r)	mtmsrd	r
#endif
#ifndef MTFSF_L
#define MTFSF_L(REG) \
	.long (0xfc00058e | ((0xff) << 17) | ((REG) << 11) | (1 << 25))
#endif
#else /* !CONFIG_PPC64 */
#ifndef MTMSRD
#define MTMSRD(r)	mtmsr	r
#endif
#ifndef MTFSF_L
#define MTFSF_L(REG)	mtfsf	0xff, (REG)
#endif
#endif /* !CONFIG_PPC64 */

/*
 * The following accessor has to work with ancient kernel versions,
 * for which THREAD_FPSCR is not usable for 64bit save/restore.
 */
#define __FPSCR(base)	THREAD_FPR0+32*8(base)

/* r3 = &thread_struct (tcb->fpup) */
_GLOBAL(rthal_save_fpu)
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	MTMSRD(r5)
	isync
	SAVE_32FPRS(0,r3)
	mffs	fr0
	stfd	fr0,__FPSCR(r3)
	blr

/* r3 = &thread_struct */
_GLOBAL(rthal_init_fpu)
	mfmsr	r5
	ori	r5,r5,MSR_FP|MSR_FE0|MSR_FE1
	SYNC
	MTMSRD(r5)

	/* Fallback wanted. */
	
/* r3 = &thread_struct (tcb->fpup) */
_GLOBAL(rthal_restore_fpu)
	mfmsr	r5
	ori	r5,r5,MSR_FP
	SYNC
	MTMSRD(r5)
	isync
	lfd	fr0,__FPSCR(r3)
	MTFSF_L(fr0)
	REST_32FPRS(0,r3)
	blr
