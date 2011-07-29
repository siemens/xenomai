/*
 * Copyright (C) 2008, 2009 Jan Kiszka <jan.kiszka@siemens.com>.
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

#include <signal.h>
#include <stdlib.h>

#include "internal.h"

#include <nucleus/thread.h>
#include <asm-generic/syscall.h>
#include <asm-generic/bits/current.h>

static void assert_nrt_inner(void)
{
	xnthread_info_t info;
	int err;

	err = XENOMAI_SYSCALL1(__xn_sys_current_info, &info);

	if (err) {
		fprintf(stderr, "__xn_sys_current_info failed: %s\n",
			strerror(-err));
		return;
	}

	if (info.state & XNTRAPSW)
		pthread_kill(pthread_self(), SIGXCPU);
}

void assert_nrt(void)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		assert_nrt_inner();
}

/*
 * Note: Works without syscalls but may not catch all errors when used inside
 * TSD destructors (as registered via pthread_key_create) when TLS support
 * (__thread) is disabled.
 */
void assert_nrt_fast(void)
{
	if (unlikely(xeno_get_current_fast() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		assert_nrt_inner();
}

/* Memory allocation services */
void *__wrap_malloc(size_t size)
{
	assert_nrt();
	return __real_malloc(size);
}

void __wrap_free(void *ptr)
{
	assert_nrt();
	__real_free(ptr);
}

/* vsyscall-based services */
int __wrap_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	assert_nrt();
	return __real_gettimeofday(tv, tz);
}

/*
 * clock_gettime is vsyscall-based as well, but we already provide a wrapper
 * via the posix skin. A second version here, even if tagged weak, easily
 * causes troubles if a certain linking order is not strictly applied.
 * Therefore, no debug helper is provided via the libxenomai.
 */
