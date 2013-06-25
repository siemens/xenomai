#ifndef _LIB_COBALT_CURRENT_H
#define _LIB_COBALT_CURRENT_H

#include <pthread.h>
#include <cobalt/uapi/thread.h>

extern pthread_key_t cobalt_current_window_key;

xnhandle_t cobalt_get_current_slow(void);

#ifdef HAVE_TLS
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
xnhandle_t cobalt_current;
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct xnthread_user_window *cobalt_current_window;

static inline xnhandle_t cobalt_get_current(void)
{
	return cobalt_current;
}

static inline xnhandle_t cobalt_get_current_fast(void)
{
	return cobalt_get_current();
}

static inline unsigned long cobalt_get_current_mode(void)
{
	return cobalt_current_window ? cobalt_current_window->state : XNRELAX;
}

static inline struct xnthread_user_window *cobalt_get_current_window(void)
{
	return cobalt_current ? cobalt_current_window : NULL;
}

#else /* ! HAVE_TLS */
extern pthread_key_t cobalt_current_key;

xnhandle_t cobalt_get_current_slow(void);

static inline xnhandle_t cobalt_get_current(void)
{
	void *val = pthread_getspecific(cobalt_current_key);

	return (xnhandle_t)val ?: cobalt_get_current_slow();
}

/* syscall-free, but unreliable in TSD destructor context */
static inline xnhandle_t cobalt_get_current_fast(void)
{
	void *val = pthread_getspecific(cobalt_current_key);

	return (xnhandle_t)val ?: XN_NO_HANDLE;
}

static inline unsigned long cobalt_get_current_mode(void)
{
	struct xnthread_user_window *window;

	window = pthread_getspecific(cobalt_current_window_key);

	return window ? window->state : XNRELAX;
}

static inline struct xnthread_user_window *cobalt_get_current_window(void)
{
	return pthread_getspecific(cobalt_current_window_key);
}

#endif /* ! HAVE_TLS */

void cobalt_init_current_keys(void);

void cobalt_set_current(void);

void cobalt_set_current_window(unsigned long offset);

#endif /* _LIB_COBALT_CURRENT_H */
