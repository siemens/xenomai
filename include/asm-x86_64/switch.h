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

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

struct 	xnarch_x8664_swregs {

	/* switch frame */
	unsigned long rbp;
	unsigned long eflags;
	/* thread entry data */
	unsigned long i_arg;
};

#define __SWITCH_CLOBBER_LIST  , "rbx", "r12", "r13", "r14", "r15"

#define xnarch_switch_threads(prev,next,last,p_rsp,n_rsp)		\
	asm volatile("pushfq\n\t"					\
		     "pushq	%%rbp\n\t"				\
		     "movq	%%rsi, %%rbp\n\t"			\
		     "movq	%%rsp, (%%rdx)\n\t"			\
		     "movq	(%%rcx), %%rsp\n\t"			\
		     "testq	%%rsi, %%rsi\n\t"			\
		     "jz	1f\n\t"					\
		     "cmpq	%%rdi, %%rsi\n\t"			\
		     "jz	1f\n\t"					\
		     "call	__switch_to\n\t"			\
		     "1:\n\t"						\
		     "movq	%%rbp, %%rsi\n\t"			\
		     "popq	%%rbp\n\t"				\
		     "popfq\n\t"					\
		     "testq	%%rbp, %%rbp\n\t"			\
		     "jnz	2f\n\t"					\
		     "popq	%%rdi\n\t"				\
		     "jmp	xnarch_thread_trampoline\n\t"		\
		     "2:\n\t"						\
		     : "=a" (last)					\
		     : "S" (next), "D" (prev), "d" (p_rsp), "c" (n_rsp)	\
		     : "memory", "cc" __SWITCH_CLOBBER_LIST)

#define xnarch_switch_clobber()		\
	asm volatile(""				\
		     : /* no output */		\
		     : /* no input */		\
		     : "cc", "memory" __SWITCH_CLOBBER_LIST)

#endif /* !_XENO_ASM_X86_64_SWITCH_H */
