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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>		/* For pthread_setcanceltype. */
#include <posix/syscall.h>
#include <time.h>
#include <asm/xenomai/arith.h>
#include <asm-generic/xenomai/timeconv.h>
#include <sys/types.h>
#include <nucleus/vdso.h>

extern int __pse51_muxid;

#ifdef XNARCH_HAVE_NONPRIV_TSC
static xnsysinfo_t __pse51_sysinfo;

void pse51_clock_init(int muxid)
{
	int err = -XENOMAI_SYSCALL2(__xn_sys_info, muxid, &__pse51_sysinfo);
	if (err) {
		fprintf(stderr, "Xenomai Posix skin init: "
			"sys_info: %s\n", strerror(err));
		exit(EXIT_FAILURE);
	}
}
#endif /* XNARCH_HAVE_NONPRIV_TSC */

int __wrap_clock_getres(clockid_t clock_id, struct timespec *tp)
{
	int err = -XENOMAI_SKINCALL2(__pse51_muxid,
				     __pse51_clock_getres,
				     clock_id,
				     tp);

	if (!err)
		return 0;

	errno = err;
	return -1;
}

#ifdef XNARCH_HAVE_NONPRIV_TSC
static int __do_clock_host_realtime(struct timespec *ts, void *tzp)
{
	unsigned int seq;
	unsigned long long now, base, mask, cycle_delta;
	unsigned long mult, shift, nsec, rem;
	struct xnvdso_hostrt_data *hostrt_data;

	if (!xnvdso_test_feature(XNVDSO_FEAT_HOST_REALTIME))
		return -1;

	hostrt_data = &nkvdso->hostrt_data;

	if (unlikely(!hostrt_data->live))
		return -1;

	/*
	 * The following is essentially a verbatim copy of the
	 * mechanism in the kernel.
	 */
retry:
	seq = xnread_seqcount_begin(&hostrt_data->seqcount);

	now = __xn_rdtsc();
	base = hostrt_data->cycle_last;
	mask = hostrt_data->mask;
	mult = hostrt_data->mult;
	shift = hostrt_data->shift;
	ts->tv_sec = hostrt_data->wall_time_sec;
	nsec = hostrt_data->wall_time_nsec;

	/* If the data changed during the read, try the
	   alternative data element */
	if (xnread_seqcount_retry(&hostrt_data->seqcount, seq))
		goto retry;

	cycle_delta = (now - base) & mask;
	nsec += (cycle_delta * mult) >> shift;

	ts->tv_sec += xnarch_divrem_billion(nsec, &rem);
	ts->tv_nsec = rem;

	return 0;
}
#endif /* XNARCH_HAVE_NONPRIV_TSC */

int __wrap_clock_gettime(clockid_t clock_id, struct timespec *tp)
{
	int err;

	switch (clock_id) {
#ifdef XNARCH_HAVE_NONPRIV_TSC
	case CLOCK_HOST_REALTIME:
		err = __do_clock_host_realtime(tp, NULL);
		break;
	case CLOCK_MONOTONIC:
		if (__pse51_sysinfo.tickval == 1) {
			unsigned long long ns;
			unsigned long rem;

			ns = xnarch_tsc_to_ns(__xn_rdtsc());
			tp->tv_sec = xnarch_divrem_billion(ns, &rem);
			tp->tv_nsec = rem;
			return 0;
		}
		/* Falldown wanted */
#endif /* XNARCH_HAVE_NONPRIV_TSC */
	default:
		err = -XENOMAI_SKINCALL2(__pse51_muxid,
					 __pse51_clock_gettime,
					 clock_id,
					 tp);
	}

	if (!err)
		return 0;

	errno = err;
	return -1;
}

int __wrap_clock_settime(clockid_t clock_id, const struct timespec *tp)
{
	int err = -XENOMAI_SKINCALL2(__pse51_muxid,
				     __pse51_clock_settime,
				     clock_id,
				     tp);

	if (!err)
		return 0;

	errno = err;
	return -1;
}

int __wrap_clock_nanosleep(clockid_t clock_id,
			   int flags,
			   const struct timespec *rqtp, struct timespec *rmtp)
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = -XENOMAI_SKINCALL4(__pse51_muxid,
				 __pse51_clock_nanosleep,
				 clock_id, flags, rqtp, rmtp);

	pthread_setcanceltype(oldtype, NULL);

	return err;
}

int __wrap_nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
	int err = __wrap_clock_nanosleep(CLOCK_REALTIME, 0, rqtp, rmtp);

	if (!err)
		return 0;

	errno = err;
	return -1;
}
