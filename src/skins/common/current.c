/*
 * Copyright (C) 2009 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <nucleus/types.h>
#include <nucleus/thread.h>
#include <nucleus/vdso.h>
#include <asm/xenomai/syscall.h>
#include <asm-generic/current.h>
#include <asm-generic/sem_heap.h>

#ifdef HAVE___THREAD
__thread __attribute__ ((tls_model ("initial-exec")))
xnhandle_t xeno_current = XN_NO_HANDLE;
__thread __attribute__ ((tls_model ("initial-exec")))
unsigned long *xeno_current_mode;

static inline void __xeno_set_current(xnhandle_t current)
{
	xeno_current = current;
}

void xeno_init_current_keys(void)
{
}

void xeno_set_current_mode(unsigned long offset)
{
	xeno_current_mode = (unsigned long *)(xeno_sem_heap[0] + offset);
}
#else /* !HAVE___THREAD */

pthread_key_t xeno_current_mode_key;
pthread_key_t xeno_current_key;

static inline void __xeno_set_current(xnhandle_t current)
{
	current = (current != XN_NO_HANDLE ? current : (xnhandle_t)(0));
	pthread_setspecific(xeno_current_key, (void *)current);
}

static void xeno_current_fork_handler(void)
{
	if (xeno_get_current() != XN_NO_HANDLE)
		__xeno_set_current(XN_NO_HANDLE);
}

static void init_current_keys(void)
{
	int err = pthread_key_create(&xeno_current_key, NULL);
	if (err)
		goto error_exit;

	pthread_atfork(NULL, NULL, &xeno_current_fork_handler);

	err = pthread_key_create(&xeno_current_mode_key, NULL);
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

void xeno_set_current_mode(unsigned long offset)
{
	pthread_setspecific(xeno_current_mode_key,
			    (void *)(xeno_sem_heap[0] + offset));
}
#endif /* !HAVE___THREAD */

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
