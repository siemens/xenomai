#ifndef _XENO_ASM_GENERIC_BITS_BIND_H
#define _XENO_ASM_GENERIC_BITS_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <nucleus/compiler.h>	/* For __constructor__ */

int 
xeno_bind_skin_opt(unsigned skin_magic, const char *skin, const char *module);

static inline int 
xeno_bind_skin(unsigned skin_magic, const char *skin, const char *module)
{
	int muxid = xeno_bind_skin_opt(skin_magic, skin, module);

	if (muxid == -1) {
		fprintf(stderr,
			"Xenomai: %s skin or CONFIG_XENO_OPT_PERVASIVE disabled.\n"
			"(modprobe %s?)\n", skin, module);
		exit(EXIT_FAILURE);
	}

	return muxid;
}

#endif /* _XENO_ASM_GENERIC_BITS_BIND_H */
