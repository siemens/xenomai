#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/xenomai/switch.h>

	.code64

/*
 * struct task_struct *rthal_thread_switch(struct thread_struct *prev,
 *					struct thread_struct *next,
 *					struct thread_struct *prev_ts,
 *					struct thread_struct *next_ts)
 */

ENTRY(rthal_switch_threads)

	pushfq
	pushq	%rbp
	movq	%rsi, %rbp
	SAVE_SWITCH_REGS
	movq	%rsp, 8(%rdx)
	movq	8(%rcx), %rsp
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
