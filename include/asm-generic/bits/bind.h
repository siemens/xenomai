#ifndef _XENO_ASM_GENERIC_BITS_BIND_H
#define _XENO_ASM_GENERIC_BITS_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

void xeno_handle_mlock_alert(int sig);

int 
xeno_bind_skin_opt(unsigned skin_magic, const char *skin, const char *module);

static inline int 
xeno_bind_skin(unsigned skin_magic, const char *skin, const char *module)
{
	int muxid = xeno_bind_skin_opt(skin_magic, skin, module);
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

#endif /* _XENO_ASM_GENERIC_BITS_BIND_H */
