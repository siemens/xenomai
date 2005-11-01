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
#include <xenomai/posix/syscall.h>
#include "posix/pthread.h"

extern int __pse51_muxid;

/* Xenomai only deals with the CLOCK_MONOTONIC clock for
   now. Calls referring to other clock types are simply routed to the
   libc. */

int __wrap_clock_getres (clockid_t clock_id, struct timespec *tp)

{
    int err = -XENOMAI_SKINCALL2(__pse51_muxid,
                                 __pse51_clock_getres,
                                 clock_id,
                                 tp);

    if(!err)
        return 0;

    errno = err;
    return -1;
}

int __wrap_clock_gettime (clockid_t clock_id, struct timespec *tp)

{
    int err = -XENOMAI_SKINCALL2(__pse51_muxid,
                                 __pse51_clock_gettime,
                                 clock_id,
                                 tp);

    if(!err)
        return 0;

    errno = err;
    return -1;
}

int __wrap_clock_settime (clockid_t clock_id, const struct timespec *tp)

{
    int err = -XENOMAI_SKINCALL2(__pse51_muxid,
                                 __pse51_clock_settime,
                                 clock_id,
                                 tp);

    if(!err)
        return 0;

    errno = err;
    return -1;
}

int __wrap_clock_nanosleep (clockid_t clock_id,
			    int flags,
			    const struct timespec *rqtp,
			    struct timespec *rmtp)
{
    int err = -XENOMAI_SKINCALL4(__pse51_muxid,
                                 __pse51_clock_nanosleep,
                                 clock_id,
                                 flags,
                                 rqtp,
                                 rmtp);

    return err;
}

int __wrap_nanosleep (const struct timespec *rqtp,
		      struct timespec *rmtp)
{
    int err = __wrap_clock_nanosleep(CLOCK_REALTIME,0,rqtp,rmtp);

    if(!err)
        return 0;

    errno = err;
    return -1;
}
