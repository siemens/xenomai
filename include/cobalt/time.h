/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COBALT_TIME_H
#define _COBALT_TIME_H

#ifdef __KERNEL__

#include <linux/time.h>

#define DELAYTIMER_MAX UINT_MAX

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

#else /* !__KERNEL__ */

#include_next <time.h>
/*
 * In case <time.h> is included as a side effect of an __need* macro,
 * include it a second time to get all definitions.
 */
#include_next <time.h>
#include <cobalt/wrappers.h>

#endif /* !__KERNEL__ */

#ifndef CLOCK_MONOTONIC
/* Some archs do not implement this, but Xenomai always does. */
#define CLOCK_MONOTONIC 1
#endif /* CLOCK_MONOTONIC */

#ifndef CLOCK_MONOTONIC_RAW
/* Linux implements this since 2.6.28. */
#define CLOCK_MONOTONIC_RAW 4
#endif /* CLOCK_MONOTONIC_RAW */

/*
 * This number is supposed to not collide with any of the POSIX and
 * Linux kernel definitions so that no ambiguities arise when porting
 * applications in both directions.
 */
#define CLOCK_HOST_REALTIME 42

#ifndef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, clock_getres(clockid_t clock_id,
			      struct timespec *tp));

COBALT_DECL(int, clock_gettime(clockid_t clock_id,
			       struct timespec *tp));

COBALT_DECL(int, clock_settime(clockid_t clock_id,
			       const struct timespec *tp));

COBALT_DECL(int, clock_nanosleep(clockid_t clock_id,
				 int flags,
				 const struct timespec *rqtp,
				 struct timespec *rmtp));

COBALT_DECL(int, nanosleep(const struct timespec *rqtp,
			   struct timespec *rmtp));

COBALT_DECL(int, timer_create(clockid_t clockid,
			      const struct sigevent *__restrict__ evp,
			      timer_t * __restrict__ timerid));

COBALT_DECL(int, timer_delete(timer_t timerid));

COBALT_DECL(int, timer_settime(timer_t timerid,
			       int flags,
			       const struct itimerspec *value,
			       struct itimerspec *ovalue));

COBALT_DECL(int, timer_gettime(timer_t timerid,
			       struct itimerspec *value));

COBALT_DECL(int, timer_getoverrun(timer_t timerid));

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* !_COBALT_TIME_H */
