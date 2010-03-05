#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>
#include <nucleus/thread.h>

extern pthread_key_t xeno_current_mode_key;

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

static inline int xeno_primary_mode(void)
{
	return xeno_current_mode & XNRELAX;
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

static inline unsigned long xeno_get_current_mode(void)
{
	unsigned long *mode = pthread_getspecific(xeno_current_mode_key);

	return mode ? *mode : -1;
}

static inline int xeno_primary_mode(void)
{
	unsigned long *mode = pthread_getspecific(xeno_current_mode_key);

	return mode ? (*mode & XNRELAX) : 0;
}
#endif /* ! HAVE___THREAD */

void xeno_set_current(void);

unsigned long *xeno_init_current_mode(void);
void xeno_init_current_keys(void);

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
