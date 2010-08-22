#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>
#include <nucleus/thread.h>

extern pthread_key_t xeno_current_mode_key;

xnhandle_t xeno_slow_get_current(void);
unsigned long xeno_slow_get_current_mode(void);
void xeno_current_warn_old(void);

#ifdef HAVE___THREAD
extern __thread xnhandle_t xeno_current __attribute__ ((tls_model ("initial-exec")));
extern __thread unsigned long
xeno_current_mode __attribute__ ((tls_model ("initial-exec")));

static inline xnhandle_t xeno_get_current(void)
{
	return xeno_current;
}

#define xeno_get_current_fast() xeno_get_current()

static inline unsigned long xeno_get_current_mode(void)
{
	unsigned long mode = xeno_current_mode;

	return mode == -1 ? xeno_slow_get_current_mode() : mode;
}

#else /* ! HAVE___THREAD */
extern pthread_key_t xeno_current_key;

xnhandle_t xeno_slow_get_current(void);

unsigned long xeno_slow_get_current_mode(void);

static inline xnhandle_t xeno_get_current(void)
{
	void *val = pthread_getspecific(xeno_current_key);

	return (xnhandle_t)val ?: xeno_slow_get_current();
}

/* syscall-free, but unreliable in TSD destructor context */
static inline xnhandle_t xeno_get_current_fast(void)
{
	void *val = pthread_getspecific(xeno_current_key);

	return (xnhandle_t)val ?: XN_NO_HANDLE;
}

static inline unsigned long xeno_get_current_mode(void)
{
	unsigned long *mode = pthread_getspecific(xeno_current_mode_key);

	return mode ? (*mode) : xeno_slow_get_current_mode();
}

#endif /* ! HAVE___THREAD */

void xeno_set_current(void);

unsigned long *xeno_init_current_mode(void);

void xeno_init_current_keys(void);

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
