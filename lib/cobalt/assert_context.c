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
#include <stdio.h>
#include <errno.h>
#include <cobalt/uapi/thread.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

static void assert_nrt_inner(void)
{
	struct cobalt_threadstat stat;
	int ret;

	ret = cobalt_thread_stat(0, &stat);
	if (ret) {
		warning("cobalt_thread_stat() failed: %s", strerror(-ret));
		return;
	}

	if (stat.status & XNWARN)
		pthread_kill(pthread_self(), SIGDEBUG);
}

void assert_nrt(void)
{
	if (!cobalt_is_relaxed())
		assert_nrt_inner();
}

void assert_nrt_fast(void)	/* OBSOLETE */
{
	assert_nrt();
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
