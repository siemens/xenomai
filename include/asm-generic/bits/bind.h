#ifndef _XENO_ASM_GENERIC_BITS_BIND_H
#define _XENO_ASM_GENERIC_BITS_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

union xnsiginfo;

typedef void xnsighandler(union xnsiginfo *si);

void xeno_handle_mlock_alert(int sig);

int 
xeno_bind_skin_opt(unsigned skin_magic, const char *skin, 
		   const char *module, xnsighandler *sighandler);

static inline int 
xeno_bind_skin(unsigned skin_magic, const char *skin, 
	       const char *module, xnsighandler *sighandler)
{
	int muxid = xeno_bind_skin_opt(skin_magic, skin, module, sighandler);
	struct sigaction sa;

	if (muxid == -1) {
		fprintf(stderr,
			"Xenomai: %s skin or CONFIG_XENO_OPT_PERVASIVE disabled.\n"
			"(modprobe %s?)\n", skin, module);
		exit(EXIT_FAILURE);
	}

	sa.sa_handler = &xeno_handle_mlock_alert;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGXCPU, &sa, NULL);

	return muxid;
}

void xeno_fault_stack(void);

#endif /* _XENO_ASM_GENERIC_BITS_BIND_H */
