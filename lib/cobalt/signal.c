/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <cobalt/uapi/signal.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

COBALT_IMPL(int, sigwait, (const sigset_t *set, int *sig))
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_sigwait, set, sig);
}

COBALT_IMPL(int, sigwaitinfo, (const sigset_t *set, siginfo_t *si))
{
	int ret;

	ret = XENOMAI_SKINCALL2(__cobalt_muxid,
				sc_cobalt_sigwaitinfo, set, si);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}

COBALT_IMPL(int, sigtimedwait, (const sigset_t *set, siginfo_t *si,
				const struct timespec *timeout))
{
	int ret;

	ret = XENOMAI_SKINCALL3(__cobalt_muxid,
				sc_cobalt_sigtimedwait, set, si, timeout);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}

COBALT_IMPL(int, sigpending, (sigset_t *set))
{
	int ret;

	ret = XENOMAI_SKINCALL1(__cobalt_muxid,
				sc_cobalt_sigpending, set);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

COBALT_IMPL(int, kill, (pid_t pid, int sig))
{
	int ret;

	/*
	 * Delegate processing of special pids to the regular
	 * kernel. We only deal with thread-directed signals.
	 */
	if (pid <= 0)
		return __real_kill(pid, sig);

	ret = XENOMAI_SKINCALL2(__cobalt_muxid,
				sc_cobalt_kill, pid, sig);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}
