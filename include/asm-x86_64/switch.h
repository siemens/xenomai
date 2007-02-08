/*
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_X86_64_SWITCH_H
#define _XENO_ASM_X86_64_SWITCH_H

#ifdef __ASSEMBLY__

.macro SAVE_SWITCH_REGS
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%r11
	pushq	%r10
	pushq	%r9
	pushq	%r8
	pushq	%rdi
	pushq	%rdx
	pushq	%rcx
	pushq	%rbx
.endm

.macro RESTORE_SWITCH_REGS
	popq	%rbx
	popq	%rcx
	popq	%rdx
	popq	%rdi
	popq	%r8
	popq	%r9
	popq	%r10
	popq	%r11
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
.endm

#else /* !__ASSEMBLY__ */

#include <linux/linkage.h>

struct 	xnarch_x8664_swregs {

	unsigned long rbx;
	unsigned long rcx;
	unsigned long rdx;
	unsigned long rdi;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long rbp;
	unsigned long eflags;
	unsigned long rip;
};

struct thread_struct;
struct task_struct;

asmlinkage struct task_struct *rthal_switch_threads(struct task_struct *prev,
						    struct task_struct *next,
						    struct thread_struct *prev_ts,
						    struct thread_struct *next_ts);
#endif /* !__ASSEMBLY__ */

#endif /* !_XENO_ASM_X86_64_SWITCH_H */
