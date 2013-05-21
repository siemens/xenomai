/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <cobalt/syscall.h>
#include <time.h>
#include "internal.h"

COBALT_IMPL(int, timer_create, (clockid_t clockid,
				const struct sigevent *__restrict__ evp,
				timer_t * __restrict__ timerid))
{
	int err = -XENOMAI_SKINCALL3(__cobalt_muxid,
				     sc_cobalt_timer_create,
				     clockid,
				     evp,
				     timerid);

	if (!err)
		return 0;

	errno = err;

	return -1;
}

COBALT_IMPL(int, timer_delete, (timer_t timerid))
{
	int err = -XENOMAI_SKINCALL1(__cobalt_muxid,
				     sc_cobalt_timer_delete,
				     timerid);

	if (!err)
		return 0;

	errno = err;

	return -1;
}

COBALT_IMPL(int, timer_settime, (timer_t timerid,
				 int flags,
				 const struct itimerspec *__restrict__ value,
				 struct itimerspec *__restrict__ ovalue))
{
	int err = -XENOMAI_SKINCALL4(__cobalt_muxid,
				     sc_cobalt_timer_settime,
				     timerid,
				     flags,
				     value,
				     ovalue);

	if (!err)
		return 0;

	errno = err;

	return -1;
}

COBALT_IMPL(int, timer_gettime, (timer_t timerid, struct itimerspec *value))
{
	int err = -XENOMAI_SKINCALL2(__cobalt_muxid,
				     sc_cobalt_timer_gettime,
				     timerid,
				     value);

	if (!err)
		return 0;

	errno = err;

	return -1;
}

COBALT_IMPL(int, timer_getoverrun, (timer_t timerid))
{
	int overrun = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_timer_getoverrun,
					timerid);

	if (overrun >= 0)
		return overrun;

	errno = -overrun;

	return -1;
}
