#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <asm/xenomai/syscall.h>
#include <nucleus/types.h>

#ifdef HAVE___THREAD
__thread __attribute__ ((tls_model ("initial-exec"), weak))
xnhandle_t xeno_current = XN_NO_HANDLE;
__thread __attribute__ ((tls_model ("initial-exec"), weak))
unsigned long xeno_current_mode;

static inline void __xeno_set_current(xnhandle_t current)
{
	xeno_current = current;
}
#else /* !HAVE___THREAD */
#include <pthread.h>

pthread_once_t xeno_init_current_keys_once __attribute__((weak));
pthread_key_t xeno_current_key __attribute__ ((weak));
pthread_key_t xeno_current_mode_key __attribute__ ((weak));

static inline void __xeno_set_current(xnhandle_t current)
{
	pthread_setspecific(xeno_current_key, (void *)current);
}

__attribute__ ((weak))
unsigned long *xeno_init_current_mode(void)
{
	unsigned long *mode = malloc(sizeof(unsigned long));
	pthread_setspecific(xeno_current_mode_key, mode);
	return mode;
}

__attribute__ ((weak)) 
void init_current_keys(void)
{
	int err = pthread_key_create(&xeno_current_key, NULL);
	if (err)
		goto error_exit;

	err = pthread_key_create(&xeno_current_mode_key, free);
	if (err) {
	  error_exit:
		fprintf(stderr, "Xenomai: error creating TSD key: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}
}

__attribute__ ((weak))
void xeno_init_current_keys(void)
{
	pthread_once(&xeno_init_current_keys_once, init_current_keys);
}
#endif /* !HAVE___THREAD */

__attribute__ ((weak))
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
