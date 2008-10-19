#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>
#include <nucleus/types.h>

extern pthread_key_t xeno_current_key;

extern void xeno_set_current(void);

static inline xnhandle_t xeno_get_current(void)
{
	void *val = pthread_getspecific(xeno_current_key);

	if (!val)
		return XN_NO_HANDLE;

	return (xnhandle_t)val;
}

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
