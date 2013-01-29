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

#define FP_FEATURE_SSE			0x01
#define FP_FEATURE_AVX			0x02

#define FP_CPUID_SSE_MASK		(1 << 25)
#define FP_CPUID_AVX_MASK		((1 << 27) | (1 << 28))

static unsigned long fp_features;

static inline void fp_features_init(void)
{
	unsigned int eax, ebx, ecx, edx;

	eax = 1;
	__asm__ __volatile__("cpuid"
		: "=a" (eax),
	          "=b" (ebx),
	          "=c" (ecx),
	          "=d" (edx)
		: "0" (eax)
		: "memory");

	if (edx & FP_CPUID_SSE_MASK)
		fp_features |= FP_FEATURE_SSE;
	/* check for both xsave and avx */
	if ((ecx & FP_CPUID_AVX_MASK) == FP_CPUID_AVX_MASK)
		fp_features |= FP_FEATURE_AVX;
}

static inline void fp_regs_set(unsigned val)
{
	uint64_t vec[4] = { val, 0, val, 0 };
	unsigned i;

	for (i = 0; i < 8; i++)
		__asm__ __volatile__("fildl %0": /* no output */ :"m"(val));
	if (fp_features & FP_FEATURE_AVX)
		__asm__ __volatile__(
			"vmovupd %0,%%ymm0;"
			"vmovupd %0,%%ymm1;"
			"vmovupd %0,%%ymm2;"
			"vmovupd %0,%%ymm3;"
			"vmovupd %0,%%ymm4;"
			"vmovupd %0,%%ymm5;"
			"vmovupd %0,%%ymm6;"
			"vmovupd %0,%%ymm7;"
			: : "m" (vec[0]));
	else if (fp_features & FP_FEATURE_SSE)
		__asm__ __volatile__(
			"movupd %0,%%xmm0;"
			"movupd %0,%%xmm1;"
			"movupd %0,%%xmm2;"
			"movupd %0,%%xmm3;"
			"movupd %0,%%xmm4;"
			"movupd %0,%%xmm5;"
			"movupd %0,%%xmm6;"
			"movupd %0,%%xmm7;"
			: : "m" (vec[0]));
}

static inline unsigned fp_regs_check(unsigned val)
{
	unsigned i, result = val;
	uint64_t vec[8][4];
	unsigned e[8];

	for (i = 0; i < 8; i++)
		__asm__ __volatile__("fistpl %0":"=m"(e[7 - i]));
	if (fp_features & FP_FEATURE_AVX) {
		__asm__ __volatile__(
			"vmovupd %%ymm0,%0;"
			"vmovupd %%ymm1,%1;"
			"vmovupd %%ymm2,%2;"
			"vmovupd %%ymm3,%3;"
			"vmovupd %%ymm4,%4;"
			"vmovupd %%ymm5,%5;"
			"vmovupd %%ymm6,%6;"
			"vmovupd %%ymm7,%7;"
			:
			: "m" (vec[0][0]), "m" (vec[1][0]),
			  "m" (vec[2][0]), "m" (vec[3][0]),
			  "m" (vec[4][0]), "m" (vec[5][0]),
			  "m" (vec[6][0]), "m" (vec[7][0]));
	} else if (fp_features & FP_FEATURE_SSE) {
		__asm__ __volatile__(
			"movupd %%xmm0,%0;"
			"movupd %%xmm1,%1;"
			"movupd %%xmm2,%2;"
			"movupd %%xmm3,%3;"
			"movupd %%xmm4,%4;"
			"movupd %%xmm5,%5;"
			"movupd %%xmm6,%6;"
			"movupd %%xmm7,%7;"
			:
			: "m" (vec[0][0]), "m" (vec[1][0]),
			  "m" (vec[2][0]), "m" (vec[3][0]),
			  "m" (vec[4][0]), "m" (vec[5][0]),
			  "m" (vec[6][0]), "m" (vec[7][0]));
	}

	for (i = 0; i < 8; i++)
		if (e[i] != val) {
			printk("r%d: %u != %u\n", i, e[i], val);
			result = e[i];
		}

	if (fp_features & FP_FEATURE_AVX) {
		for (i = 0; i < 8; i++) {
			int error = 0;
			if (vec[i][0] != val) {
				result = vec[i][0];
				error = 1;
			}
			if (vec[i][2] != val) {
				result = vec[i][2];
				error = 1;
			}
			if (error)
				printk("ymm%d: %llu/%llu != %u/%u\n",
				       i, (unsigned long long)vec[i][0],
				       (unsigned long long)vec[i][2],
				       val, val);
		}
	} else if (fp_features & FP_FEATURE_SSE) {
		for (i = 0; i < 8; i++)
			if (vec[i][0] != val) {
				printk("xmm%d: %llu != %u\n",
				       i, (unsigned long long)vec[i][0], val);
				result = vec[i][0];
			}
	}

	return result;
}

#endif /* _XENO_ASM_X86_FPTEST_H */
