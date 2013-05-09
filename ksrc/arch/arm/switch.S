/*
 * Copyright (C) 2005 Stelian Pop.
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
#include <linux/version.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#ifdef CONFIG_VFP
#include <asm/vfpmacros.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
#include <asm/tls.h>
#else
	.macro set_tls tp, tmp1, tmp2
#if defined(CONFIG_HAS_TLS_REG)
	mcr     p15, 0, \tp, c13, c0, 3          @ set TLS register
#elif !defined(CONFIG_TLS_REG_EMUL)
	mov     \tmp1, #0xffff0fff
	str 	\tp, [\tmp1, #-15]  		 @ TLS val at 0xffff0ff0
#endif
	.endm
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31)
#define ARM(x...)	x
#define THUMB(x...)
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 37)
#ifdef CONFIG_MMU
#define USE_DOMAINS
#endif /* CONFIG_MMU */
#else /* Linux >= 2.6.38 */
#ifdef CONFIG_CPU_USE_DOMAINS
#define USE_DOMAINS
#endif /* CONFIG_CPU_USE_DOMAINS */
#endif /* Linux >= 2.6.38 */

#ifdef CONFIG_CC_STACKPROTECTOR
#warning "Buffer overflow detection not supported for Xenomai on ARM"
#error "Disable CONFIG_CC_STACKPROTECTOR in your kernel configuration"
#endif

	.macro fpu_switch tmp
#ifdef CONFIG_VFP
	@ Always disable VFP so we can lazily save/restore the old
	@ state. This occurs in the context of the previous thread.
	VFPFMRX \tmp, FPEXC
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
	bic     \tmp, \tmp, #FPEXC_ENABLE
#else
	bic     \tmp, \tmp, #FPEXC_EN
#endif
	VFPFMXR FPEXC, \tmp
#endif
	.endm

	.macro clear_exclusive_monitor
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32) && __LINUX_ARM_ARCH__ >= 6
#ifdef CONFIG_CPU_MPCORE
	clrex
#else
	strex   r5, r4, [ip]                    @ Clear exclusive monitor
#endif
#endif
	.endm

	.text

#if defined(CONFIG_VFP) && defined(CONFIG_XENO_HW_FPU)
/* Copied from vfp_save_state in arch/arm/vfp/vfphw.S
 * r0 = pointer to union vfp_state, r1 = fpexc
 */
ENTRY(rthal_vfp_save)
	VFPFSTMIA	r0, r2		@ save the working registers
	VFPFMRX		r2, FPSCR	@ current status
	tst		r1, #FPEXC_EX	@ is there additional state to save?
	beq		1f
	VFPFMRX		r3, FPINST	@ FPINST (only if FPEXC.EX is set)
	tst		r1, #FPEXC_FP2V	@ is there an FPINST2 to read?
	beq		1f
	VFPFMRX		r12, FPINST2	@ FPINST2 if needed (and present)
1:
	stmia		r0, {r1, r2, r3, r12}	@ save FPEXC, FPSCR, FPINST, FPINST2
	mov		pc, lr
ENDPROC(rthal_vfp_save)
	
/* Copied from no_old_VFP_process in arch/arm/vfp/vfphw.S
 * r0 = pointer to union vfp_state
 * r1 = current cpu
 */
ENTRY(rthal_vfp_load)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0) \
	|| defined(CONFIG_VFP_3_2_BACKPORT)) && defined(CONFIG_SMP)
	str		r1, [r0, #VFP_CPU]
#endif
	VFPFLDMIA	r0, r2		@ reload the working registers while
					@ FPEXC is in a safe state
	ldmia		r0, {r1, r2, r3, r12}	@ load FPEXC, FPSCR, FPINST, FPINST2
	tst		r1, #FPEXC_EX	@ is there additional state to restore?
	beq		1f
	VFPFMXR		FPINST, r3	@ restore FPINST (only if FPEXC.EX is set)
	tst		r1, #FPEXC_FP2V	@ is there an FPINST2 to write?
	beq		1f
	VFPFMXR		FPINST2, r12	@ FPINST2 if needed (and present)
1:
	VFPFMXR		FPSCR, r2	@ restore status
	mov		pc, lr
ENDPROC(rthal_vfp_load)
#endif

/*
/*
 * Switch context routine.
 *
 * Registers according to the ARM procedure call standard:
 *   Reg    Description
 *   r0-r3  argument/scratch registers
 *   r4-r9  variable register
 *   r10=sl stack limit/variable register
 *   r11=fp frame pointer/variable register
 *   r12=ip intra-procedure-call scratch register
 *   r13=sp stack pointer (auto preserved)
 *   r14=lr link register
 *   r15=pc program counter (auto preserved)
 *
 * Copied from __switch_to, arch/arm/kernel/entry-armv.S.
 * Right now it is identical, but who knows what the
 * future reserves us...
 *
 * XXX: All the following config options are NOT tested:
 *      CONFIG_IWMMXT
 *
 *  Calling args:
 * r0 = previous task_struct, r1 = previous thread_info, r2 = next thread_info
 */
ENTRY(rthal_thread_switch)
	add     ip, r1, #TI_CPU_SAVE
	ldr     r3, [r2, #TI_TP_VALUE]
 ARM(	stmia	ip!, {r4 - sl, fp, sp, lr} )	@ Store most regs on stack
 THUMB(	stmia	ip!, {r4 - sl, fp}	   )	@ Store most regs on stack
 THUMB(	str	sp, [ip], #4		   )
 THUMB(	str	lr, [ip], #4		   )
#ifdef USE_DOMAINS
	ldr     r6, [r2, #TI_CPU_DOMAIN]
#endif

	clear_exclusive_monitor
	set_tls r3, r4, r5
#ifdef USE_DOMAINS
	mcr     p15, 0, r6, c3, c0, 0           @ Set domain register
#endif
	fpu_switch r4
 ARM(	add	r4, r2, #TI_CPU_SAVE	   )
 ARM(	ldmia	r4, {r4 - sl, fp, sp, pc}  )	@ Load all regs saved previously
 THUMB(	add	ip, r2, #TI_CPU_SAVE	   )
 THUMB(	ldmia	ip!, {r4 - sl, fp}	   )	@ Load all regs saved previously
 THUMB(	ldr	sp, [ip], #4		   )
 THUMB(	ldr	pc, [ip]		   )
ENDPROC(rthal_thread_switch)

/*
 * r4 = xnarch_thread_trampoline
 * r5 = xnarchtcb_t *
 */
ENTRY(rthal_thread_trampoline)
	mov	r0, r5
	mov	pc, r4
ENDPROC(rthal_thread_trampoline)

// vim: ts=4 et sw=4 sts=4
