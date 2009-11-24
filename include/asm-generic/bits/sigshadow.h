#ifndef _XENO_ASM_GENERIC_BITS_SIGSHADOW_H
#define _XENO_ASM_GENERIC_BITS_SIGSHADOW_H

#include <pthread.h>
#include <signal.h>

extern pthread_once_t __attribute__((weak)) xeno_sigshadow_installed;
extern struct sigaction __attribute__((weak)) xeno_saved_sigshadow_action;

void __attribute__((weak)) xeno_sigshadow_install(void);

static inline void sigshadow_install_once(void)
{
	pthread_once(&xeno_sigshadow_installed, xeno_sigshadow_install);
}
#endif /* _XENO_ASM_GENERIC_BITS_SIGSHADOW_H */
