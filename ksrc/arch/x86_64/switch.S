#include <linux/linkage.h>
#include <asm/xenomai/switch.h>

	.code64

/*
 * void rthal_thread_switch(struct thread_struct *prev, struct thread_struct *next, int knext)
 */

ENTRY(rthal_switch_threads)

	pushfq
	pushq	%rbp
	movq	%rsi, %rbp
	SAVE_SWITCH_REGS
	movq	%rsp, 4(%rdi)
	movq	4(%rsi), %rsp
	testl	%edx, %edx
	jnz	1f
	call	__switch_to
1:	
	RESTORE_SWITCH_REGS
	movq	%rbp, %rsi
	popq	%rbp
	popfq
	movq	%rdi, %rax
	ret
	
END(rthal_switch_threads)
