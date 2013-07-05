#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

static void child_fork_handler(void);

#ifdef HAVE_TLS

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
xnhandle_t cobalt_current = XN_NO_HANDLE;

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct xnthread_user_window *cobalt_current_window;

static inline void __cobalt_set_current(xnhandle_t current)
{
	cobalt_current = current;
}

static void init_current_keys(void)
{
	pthread_atfork(NULL, NULL, &child_fork_handler);
}

void cobalt_set_current_window(unsigned long offset)
{
	cobalt_current_window = (struct xnthread_user_window *)
		(cobalt_sem_heap[0] + offset);
	__cobalt_prefault(cobalt_current_window);
}

#else /* !HAVE_TLS */

pthread_key_t cobalt_current_window_key;
pthread_key_t cobalt_current_key;

static inline void __cobalt_set_current(xnhandle_t current)
{
	current = (current != XN_NO_HANDLE ? current : (xnhandle_t)(0));
	pthread_setspecific(cobalt_current_key, (void *)current);
}

static void init_current_keys(void)
{
	int err = pthread_key_create(&cobalt_current_key, NULL);
	if (err)
		goto error_exit;

	pthread_atfork(NULL, NULL, &child_fork_handler);

	err = pthread_key_create(&cobalt_current_window_key, NULL);
	if (err) {
	  error_exit:
		fprintf(stderr, "Xenomai: error creating TSD key: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}
}

void cobalt_set_current_window(unsigned long offset)
{
	struct xnthread_user_window *window;

	window = (void *)(cobalt_sem_heap[0] + offset);
	pthread_setspecific(cobalt_current_window_key, window);
	__cobalt_prefault(window);
}

#endif /* !HAVE_TLS */

static void child_fork_handler(void)
{
	if (cobalt_get_current() != XN_NO_HANDLE)
		__cobalt_set_current(XN_NO_HANDLE);
}

xnhandle_t cobalt_get_current_slow(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(sc_nucleus_current, &current);

	return err ? XN_NO_HANDLE : current;
}

void cobalt_set_current(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(sc_nucleus_current, &current);
	if (err) {
		fprintf(stderr, "Xenomai: error obtaining handle for current "
			"thread: %s\n", strerror(-err));
		exit(EXIT_FAILURE);
	}
	__cobalt_set_current(current);
}

void cobalt_init_current_keys(void)
{
	static pthread_once_t cobalt_init_current_keys_once = PTHREAD_ONCE_INIT;
	pthread_once(&cobalt_init_current_keys_once, init_current_keys);
}
