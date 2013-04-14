#ifndef _XENO_ASM_X86_BIND_H
#define _XENO_ASM_X86_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <asm-generic/xenomai/bind.h>
#include <asm/xenomai/features.h>

#ifdef __i386__
static inline void xeno_x86_features_check(struct xnfeatinfo *finfo)
{
#ifdef CONFIG_XENO_X86_SEP
	size_t n = confstr(_CS_GNU_LIBPTHREAD_VERSION, NULL, 0);
	if (n > 0) {
		char buf[n];

		confstr (_CS_GNU_LIBPTHREAD_VERSION, buf, n);

		if (strstr (buf, "NPTL"))
			return;
	}

	fprintf(stderr,
"Xenomai: --enable-x86-sep needs NPTL and Linux 2.6.x or higher,\n"
"which does not match your configuration. Please upgrade, or\n"
"rebuild the user-space support passing --disable-x86-sep.\n");
	exit(1);
#endif /* CONFIG_XENO_X86_SEP */
}
#define xeno_arch_features_check(finfo) xeno_x86_features_check(finfo)

#endif /* __i386__ */

#endif /* _XENO_ASM_X86_BIND_H */
