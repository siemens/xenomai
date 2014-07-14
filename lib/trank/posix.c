/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#include <xeno_config.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <memory.h>
#include <boilerplate/ancillaries.h>
#include <boilerplate/signal.h>
#include <trank/posix/pthread.h>
#include "cobalt/internal.h"
#include "internal.h"

/**
 * @ingroup trank
 * @{
 */

/**
 * Make a thread periodic (compatibility service).
 *
 * This service makes the POSIX @a thread periodic.
 *
 * @param thread thread to arm a periodic timer for.
 *
 * @param starttp start time, expressed as an absolute value of the
 * CLOCK_REALTIME clock.
 *
 * @param periodtp period, expressed as a time interval.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 * - ETIMEDOUT, the start time has already passed.
 * - EPERM, the caller is not a Xenomai thread.
 * - EINVAL, @a thread does not refer to the current thread.
 *
 * @note Unlike the original Xenomai 2.x call, this emulation does not
 * delay the caller waiting for the first periodic release point. In
 * addition, @a thread must be equal to pthread_self().
 *
 * @deprecated This service is a non-portable extension of the Xenomai
 * 2.x POSIX interface, not available with Xenomai 3.x.  Instead,
 * Cobalt-based applications should set up a periodic timer using the
 * timer_create(), timer_settime() call pair, then wait for release
 * points via sigwaitinfo(). Overruns can be detected by looking at
 * the siginfo.si_overrun field.  Alternatively, applications may
 * obtain a file descriptor referring to a Cobalt timer via the
 * timerfd() call, and read() from it to wait for timeouts.
 */
int pthread_make_periodic_np(pthread_t thread,
			     struct timespec *starttp,
			     struct timespec *periodtp)
{
	struct trank_context *tc;
	struct itimerspec its;
	struct sigevent sev;
	int ret;

	tc = trank_get_context();
	if (tc == NULL)
		return EPERM;

	if (thread != pthread_self())
		return EINVAL;

	if (tc->periodic_timer == NULL) {
		memset(&sev, 0, sizeof(sev));
		sev.sigev_signo = SIGPERIOD;
		sev.sigev_notify = SIGEV_SIGNAL|SIGEV_THREAD_ID;
		sev.sigev_notify_thread_id = cobalt_thread_pid(thread);
		ret = __RT(timer_create(CLOCK_REALTIME, &sev,
					&tc->periodic_timer));
		if (ret)
			return -errno;
	}

	its.it_value = *starttp;
	its.it_interval = *periodtp;

	ret = __RT(timer_settime(tc->periodic_timer, TIMER_ABSTIME, &its, NULL));
	if (ret)
		return -errno;

	return 0;
}

/**
 * Wait for the next periodic release point (compatibility service)
 *
 * Delay the current thread until the next periodic release point is
 * reached. The periodic timer should have been previously started for
 * @a thread by a call to pthread_make_periodic_np().
 *
 * @param overruns_r If non-NULL, @a overruns_r shall be a pointer to
 * a memory location which will be written with the count of pending
 * overruns. This value is written to only when pthread_wait_np()
 * returns ETIMEDOUT or success. The memory location remains
 * unmodified otherwise. If NULL, this count will not be returned.
 *
 * @return Zero is returned upon success. If @a overruns_r is
 * non-NULL, zero is written to the pointed memory
 * location. Otherwise:
 *
 * - EWOULDBLOCK is returned if pthread_make_periodic_np() was not
 * called for the current thread.
 *
 * - EINTR is returned if @a thread was interrupted by a signal before
 * the next periodic release point was reached.
 *
 * - ETIMEDOUT is returned if a timer overrun occurred, which
 * indicates that a previous release point was missed by the calling
 * thread. If @a overruns_r is non-NULL, the count of pending overruns
 * is written to the pointed memory location.
 *
 * - EPERM is returned if this service was called from an invalid
 * context.
 *
 * @note If the current release point has already been reached at the
 * time of the call, the current thread immediately returns from this
 * service with no delay.
 *
 * @deprecated This service is a non-portable extension of the Xenomai
 * 2.x POSIX interface, not available with Xenomai 3.x.  Instead,
 * Cobalt-based applications should set up a periodic timer using the
 * timer_create(), timer_settime() call pair, then wait for release
 * points via sigwaitinfo(). Overruns can be detected by looking at
 * the siginfo.si_overrun field.  Alternatively, applications may
 * obtain a file descriptor referring to a Cobalt timer via the
 * timerfd() call, and read() from it to wait for timeouts.
 */
int pthread_wait_np(unsigned long *overruns_r)
{
	struct trank_context *tc;
	siginfo_t si;
	int sig;

	tc = trank_get_context();
	if (tc == NULL)
		return EPERM;

	if (tc->periodic_timer == NULL)
		return EWOULDBLOCK;

	for (;;) {
		sig = __RT(sigwaitinfo(&trank_sigperiod_set, &si));
		if (sig == SIGPERIOD)
			break;
		if (errno == EINTR)
			return EINTR;
		panic("cannot wait for next period, %s", symerror(-errno));
	}

	if (overruns_r)
		*overruns_r = si.si_overrun;

	return 0;
}

/** @} */
