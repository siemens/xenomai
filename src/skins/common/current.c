#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <nucleus/types.h>
#include <nucleus/thread.h>
#include <nucleus/vdso.h>
#include <asm/xenomai/syscall.h>
#include <asm-generic/bits/current.h>

pthread_key_t xeno_current_mode_key;

#ifdef HAVE___THREAD
__thread __attribute__ ((tls_model ("initial-exec")))
xnhandle_t xeno_current = XN_NO_HANDLE;
__thread __attribute__ ((tls_model ("initial-exec")))
unsigned long xeno_current_mode;

static inline int create_current_key(void) { return 0; }

static inline void __xeno_set_current(xnhandle_t current)
{
	xeno_current = current;
}

static inline unsigned long *create_current_mode(void)
{
	return &xeno_current_mode;
}

static inline void free_current_mode(unsigned long *mode) { }

#define XENO_MODE_LEAK_WARNING \
	"Xenomai: WARNING, this version of Xenomai kernel is anterior to" \
	" 2.5.2.\nIt can cause memory corruption on thread termination.\n" \
	"Upgrade is recommended.\n"

#else /* !HAVE___THREAD */

pthread_key_t xeno_current_key;

static inline int create_current_key(void)
{
	return pthread_key_create(&xeno_current_key, NULL);
}

static inline void __xeno_set_current(xnhandle_t current)
{
	current = (current != XN_NO_HANDLE ? current : (xnhandle_t)(0));
	pthread_setspecific(xeno_current_key, (void *)current);
}

static inline unsigned long *create_current_mode(void)
{
	return malloc(sizeof(unsigned long));
}

static inline void free_current_mode(unsigned long *mode)
{
	free(mode);
}

#define XENO_MODE_LEAK_WARNING \
	"Xenomai: WARNING, this version of Xenomai kernel is anterior to" \
	" 2.5.2.\nIn order to avoid getting memory corruption on thread " \
	"termination, we leak\nup to 8 bytes per thread. Upgrade is " \
	"recommended.\n"

#endif /* !HAVE___THREAD */

void xeno_current_warn_old(void)
{
	fprintf(stderr, XENO_MODE_LEAK_WARNING);
}

static void cleanup_current_mode(void *key)
{
	unsigned long *mode = key;

	*mode = -1;

	if (xnvdso_test_feature(XNVDSO_FEAT_DROP_U_MODE)) {
		XENOMAI_SYSCALL0(__xn_sys_drop_u_mode);
		free_current_mode(mode);
	}
}

static void xeno_current_fork_handler(void)
{
	if (xeno_get_current() != XN_NO_HANDLE)
		__xeno_set_current(XN_NO_HANDLE);
}

static void init_current_keys(void)
{
	int err = create_current_key();
	if (err)
		goto error_exit;

	pthread_atfork(NULL, NULL, &xeno_current_fork_handler);

	err = pthread_key_create(&xeno_current_mode_key, cleanup_current_mode);
	if (err) {
	  error_exit:
		fprintf(stderr, "Xenomai: error creating TSD key: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}
}

void xeno_init_current_keys(void)
{
	static pthread_once_t xeno_init_current_keys_once = PTHREAD_ONCE_INIT;
	pthread_once(&xeno_init_current_keys_once, init_current_keys);
}

xnhandle_t xeno_slow_get_current(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(__xn_sys_current, &current);

	return err ? XN_NO_HANDLE : current;
}

void xeno_set_current(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(__xn_sys_current, &current);
	if (err) {
		fprintf(stderr, "Xenomai: error obtaining handle for current "
			"thread: %s\n", strerror(-err));
		exit(EXIT_FAILURE);
	}
	__xeno_set_current(current);
}

unsigned long *xeno_init_current_mode(void)
{
	unsigned long *mode = create_current_mode();

	pthread_setspecific(xeno_current_mode_key, mode);
	return mode;
}

unsigned long xeno_slow_get_current_mode(void)
{
	xnthread_info_t info;
	int err;

	err = XENOMAI_SYSCALL1(__xn_sys_current_info, &info);
	if (err < 0)
		return XNRELAX;

	return info.state & XNRELAX;
}
