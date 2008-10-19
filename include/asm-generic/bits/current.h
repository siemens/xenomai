#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>
#include <nucleus/types.h>

#ifdef HAVE___THREAD
extern __thread xnhandle_t xeno_current __attribute__ ((tls_model ("initial-exec")));

static inline xnhandle_t xeno_get_current(void)
{
	return xeno_current;
}

#else /* ! HAVE___THREAD */
extern pthread_key_t xeno_current_key;

static inline xnhandle_t xeno_get_current(void)
{
	void *val = pthread_getspecific(xeno_current_key);

	if (!val)
		return XN_NO_HANDLE;

	return (xnhandle_t)val;
}
#endif /* ! HAVE___THREAD */

void xeno_set_current(void);

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
