#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>
#include <nucleus/types.h>

#ifdef HAVE___THREAD
extern __thread xnhandle_t xeno_current __attribute__ ((tls_model ("initial-exec")));
extern __thread unsigned long
xeno_current_mode __attribute__ ((tls_model ("initial-exec")));

static inline xnhandle_t xeno_get_current(void)
{
	return xeno_current;
}

static inline unsigned long xeno_get_current_mode(void)
{
	return xeno_current_mode;
}

static inline unsigned long *xeno_init_current_mode(void)
{
	return &xeno_current_mode;
}

#else /* ! HAVE___THREAD */
extern pthread_key_t xeno_current_key;
extern pthread_key_t xeno_current_mode_key;

static inline xnhandle_t xeno_get_current(void)
{
	void *val = pthread_getspecific(xeno_current_key);

	if (!val)
		return XN_NO_HANDLE;

	return (xnhandle_t)val;
}

static inline unsigned long xeno_get_current_mode(void)
{
	return *(unsigned long *)pthread_getspecific(xeno_current_mode_key);
}

unsigned long *xeno_init_current_mode(void);
#endif /* ! HAVE___THREAD */

void xeno_set_current(void);

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
