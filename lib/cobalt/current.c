#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <cobalt/kernel/types.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/vdso.h>
#include <asm/xenomai/syscall.h>
#include <asm-generic/current.h>
#include "internal.h"

extern unsigned long xeno_sem_heap[2];

static void child_fork_handler(void);

#ifdef HAVE_TLS

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
xnhandle_t xeno_current = XN_NO_HANDLE;

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct xnthread_user_window *xeno_current_window;

static inline void __xeno_set_current(xnhandle_t current)
{
	xeno_current = current;
}

static void init_current_keys(void)
{
	pthread_atfork(NULL, NULL, &child_fork_handler);
}

void xeno_set_current_window(unsigned long offset)
{
	xeno_current_window = (struct xnthread_user_window *)
		(xeno_sem_heap[0] + offset);
	__cobalt_prefault(xeno_current_window);
}

#else /* !HAVE_TLS */

pthread_key_t xeno_current_window_key;
pthread_key_t xeno_current_key;

static inline void __xeno_set_current(xnhandle_t current)
{
	current = (current != XN_NO_HANDLE ? current : (xnhandle_t)(0));
	pthread_setspecific(xeno_current_key, (void *)current);
}

static void init_current_keys(void)
{
	int err = pthread_key_create(&xeno_current_key, NULL);
	if (err)
		goto error_exit;

	pthread_atfork(NULL, NULL, &child_fork_handler);

	err = pthread_key_create(&xeno_current_window_key, NULL);
	if (err) {
	  error_exit:
		fprintf(stderr, "Xenomai: error creating TSD key: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}
}

void xeno_set_current_window(unsigned long offset)
{
	struct xnthread_user_window *window;

	window = (void *)(xeno_sem_heap[0] + offset);
	pthread_setspecific(xeno_current_window_key, window);
	__cobalt_prefault(window);
}

#endif /* !HAVE_TLS */

static void child_fork_handler(void)
{
	if (xeno_get_current() != XN_NO_HANDLE)
		__xeno_set_current(XN_NO_HANDLE);
}

xnhandle_t xeno_slow_get_current(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(sc_nucleus_current, &current);

	return err ? XN_NO_HANDLE : current;
}

void xeno_set_current(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(sc_nucleus_current, &current);
	if (err) {
		fprintf(stderr, "Xenomai: error obtaining handle for current "
			"thread: %s\n", strerror(-err));
		exit(EXIT_FAILURE);
	}
	__xeno_set_current(current);
}

void xeno_init_current_keys(void)
{
	static pthread_once_t xeno_init_current_keys_once = PTHREAD_ONCE_INIT;
	pthread_once(&xeno_init_current_keys_once, init_current_keys);
}
