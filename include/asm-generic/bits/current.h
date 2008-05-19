#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>

extern pthread_key_t xeno_current_key;

extern void xeno_set_current(void);

static inline void *xeno_get_current(void)
{
	return pthread_getspecific(xeno_current_key);
}

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
