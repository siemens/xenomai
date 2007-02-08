#include <linux/linkage.h>
#include <asm/xenomai/switch.h>

	.code64

/*
 * struct task_struct *rthal_thread_switch(struct thread_struct *prev,
 *					struct thread_struct *next,
 *					unsigned long *p_rsp,
 *					unsigned long *n_rsp)
 */

ENTRY(rthal_switch_threads)

	pushfq
	pushq	%rbp
	movq	%rsi, %rbp
	SAVE_SWITCH_REGS
	movq	%rsp, (%rdx)
	movq	(%rcx), %rsp
	testq	%rsi, %rsi
	jz	1f
	cmpq	%rsi, %rsi
	jz	1f
	call	__switch_to
1:
	RESTORE_SWITCH_REGS
	movq	%rbp, %rsi
	popq	%rbp
	popfq
	movq	%rdi, %rax
	ret
	
END(rthal_switch_threads)
