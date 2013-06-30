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

#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <rtdk.h>
#include <cobalt/uapi/thread.h>
#include <asm/sysdeps/syscall.h>
#include "current.h"
#include "internal.h"

static void assert_nrt_inner(void)
{
	struct xnthread_info info;
	int ret;

	ret = XENOMAI_SYSCALL1(sc_nucleus_current_info, &info);
	if (ret) {
		report_error("sc_nucleus_current_info failed: %s",
			strerror(-ret));
		return;
	}

	if (info.state & XNTRAPSW)
		pthread_kill(pthread_self(), SIGXCPU);
}

void assert_nrt(void)
{
	if (cobalt_get_current() != XN_NO_HANDLE &&
		     !(cobalt_get_current_mode() & XNRELAX))
		assert_nrt_inner();
}

/*
 * Note: Works without syscalls but may not catch all errors when used inside
 * TSD destructors (as registered via pthread_key_create) when TLS support
 * (__thread) is disabled.
 */
void assert_nrt_fast(void)
{
	if (cobalt_get_current_fast() != XN_NO_HANDLE &&
		     !(cobalt_get_current_mode() & XNRELAX))
		assert_nrt_inner();
}

/* Memory allocation services */
COBALT_IMPL(void *, malloc, (size_t size))
{
	assert_nrt();
	return __STD(malloc(size));
}

COBALT_IMPL(void, free, (void *ptr))
{
	assert_nrt();
	__STD(free(ptr));
}

/* vsyscall-based services */
COBALT_IMPL(int, gettimeofday, (struct timeval *tv, struct timezone *tz))
{
	assert_nrt();
	return __STD(gettimeofday(tv, tz));
}
