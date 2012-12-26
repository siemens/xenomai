#ifndef _XENO_ASM_X86_IPIPE_SETUP_H
#define _XENO_ASM_X86_IPIPE_SETUP_H

#include <asm/processor.h>

#ifdef cpu_has_xsave
/*
 * We don't handle the extended processor state yet. Disable
 * xsave/xrstor to keep a correct behavior.
 */
static inline void __ipipe_early_client_setup(void)
{
	if (cpu_has_xsave) {
		setup_clear_cpu_cap(X86_FEATURE_XSAVE);
		setup_clear_cpu_cap(X86_FEATURE_XSAVEOPT);
		printk(KERN_INFO "Xenomai: forcing noxsave");
	}
}

#endif	/* cpu_has_xsave */

#endif /* !_XENO_ASM_X86_IPIPE_SETUP_H */
