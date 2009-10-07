#ifndef _XENO_ASM_X86_FPTEST_H
#define _XENO_ASM_X86_FPTEST_H

#ifdef __KERNEL__
#include <linux/module.h>
#include <asm/i387.h>

static inline int fp_kernel_supported(void)
{
	return 1;
}

static inline int fp_linux_begin(void)
{
#if defined(CONFIG_X86_USE_3DNOW) \
	|| defined(CONFIG_MD_RAID456) || defined(CONFIG_MD_RAID456_MODULE)
	/* Ther kernel uses x86 FPU, we can not also use it in our tests. */
	static int once = 0;
	if (!once) {
		once = 1;
		printk("%s:%d: Warning: Linux is compiled to use FPU in "
		       "kernel-space.\nFor this reason, switchtest can not "
		       "test using FPU in Linux kernel-space.\n",
		       __FILE__, __LINE__);
	}
	return -EBUSY;
#endif /* 3DNow or RAID 456 */
	kernel_fpu_begin();
	/* kernel_fpu_begin() does no re-initialize the fpu context, but
	   fp_regs_set() implicitely expects an initialized fpu context, so
	   initialize it here. */
	__asm__ __volatile__("fninit");
	return 0;
}

static inline void fp_linux_end(void)
{
	kernel_fpu_end();
}

#else /* !__KERNEL__ */
#include <stdio.h>
#define printk printf
#endif /* !__KERNEL__ */

static inline void fp_regs_set(unsigned val)
{
	unsigned i;

	for (i = 0; i < 8; i++)
		__asm__ __volatile__("fildl %0": /* no output */ :"m"(val));
}

static inline unsigned fp_regs_check(unsigned val)
{
	unsigned i, result = val;
	unsigned e[8];

	for (i = 0; i < 8; i++)
		__asm__ __volatile__("fistpl %0":"=m"(e[7 - i]));

	for (i = 0; i < 8; i++)
		if (e[i] != val) {
			printk("r%d: %u != %u\n", i, e[i], val);
			result = e[i];
		}

	return result;
}


#endif /* _XENO_ASM_X86_FPTEST_H */
