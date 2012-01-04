#ifndef _XENO_ASM_GENERIC_CURRENT_H
#define _XENO_ASM_GENERIC_CURRENT_H

#include <pthread.h>
#include <nucleus/thread.h>

extern pthread_key_t xeno_current_window_key;

xnhandle_t xeno_slow_get_current(void);

#ifdef HAVE_TLS
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
xnhandle_t xeno_current;
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct xnthread_user_window *xeno_current_window;

static inline xnhandle_t xeno_get_current(void)
{
	return xeno_current;
}

#define xeno_get_current_fast() xeno_get_current()

static inline unsigned long xeno_get_current_mode(void)
{
	return xeno_current_window ? xeno_current_window->state : XNRELAX;
}

static inline struct xnthread_user_window *xeno_get_current_window(void)
{
	return xeno_current ? xeno_current_window : NULL;
}

#else /* ! HAVE_TLS */
extern pthread_key_t xeno_current_key;

xnhandle_t xeno_slow_get_current(void);

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
	struct xnthread_user_window *window;

	window = pthread_getspecific(xeno_current_window_key);

	return window ? window->state : XNRELAX;
}

static inline struct xnthread_user_window *xeno_get_current_window(void)
{
	return pthread_getspecific(xeno_current_window_key);
}

#endif /* ! HAVE_TLS */

void xeno_init_current_keys(void);

void xeno_set_current(void);

void xeno_set_current_window(unsigned long offset);

#endif /* _XENO_ASM_GENERIC_CURRENT_H */
