/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

/**
 * @ingroup cobalt
 * @defgroup cobalt_time Clocks and timers services.
 *
 * Clocks and timers services.
 *
 * Cobalt supports three clocks:
 *
 * CLOCK_REALTIME maps to the nucleus system clock, keeping time as the amount
 * of time since the Epoch, with a resolution of one nanosecond.
 *
 * CLOCK_MONOTONIC maps to an architecture-dependent high resolution
 * counter, so is suitable for measuring short time
 * intervals. However, when used for sleeping (with
 * clock_nanosleep()), the CLOCK_MONOTONIC clock has a resolution of
 * one nanosecond, like the CLOCK_REALTIME clock.
 *
 * CLOCK_MONOTONIC_RAW is Linux-specific, and provides monotonic time
 * values from a hardware timer which is not adjusted by NTP. This is
 * strictly equivalent to CLOCK_MONOTONIC with Xenomai, which is not
 * NTP adjusted either.
 *
 * Timer objects may be created with the timer_create() service using
 * either of the two clocks. The resolution of these timers is one
 * nanosecond, as is the case for clock_nanosleep().
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_08.html#tag_02_08_05">
 * Specification.</a>
 *
 *@{*/

#include <cobalt/kernel/vdso.h>
#include <asm/xenomai/arith.h>
#include "internal.h"
#include "thread.h"
#include "clock.h"

/**
 * Read the host-synchronised realtime clock.
 *
 * Obtain the current time with NTP corrections from the Linux domain
 *
 * @param tp pointer to a struct timespec
 *
 * @retval 0 on success;
 * @retval -1 if no suitable NTP-corrected clocksource is availabel
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/gettimeofday.html">
 * Specification.</a>
 *
 */
static int do_clock_host_realtime(struct timespec *tp)
{
#ifdef CONFIG_XENO_OPT_HOSTRT
	struct xnvdso_hostrt_data *hostrt_data;
	cycle_t now, base, mask, cycle_delta;
	unsigned long mult, shift, nsec, rem;
	urwstate_t tmp;

	hostrt_data = get_hostrt_data();
	BUG_ON(!hostrt_data);

	if (unlikely(!hostrt_data->live))
		return -1;

	/*
	 * Note: Disabling HW interrupts around writes to hostrt_data
	 * ensures that a reader (on the Xenomai side) cannot
	 * interrupt a writer (on the Linux kernel side) on the same
	 * CPU.  The urw block is required when a reader is
	 * interleaved by a writer on a different CPU. This follows
	 * the approach from userland, where taking the spinlock is
	 * not possible.
	 */
	unsynced_read_block(&tmp, &hostrt_data->lock) {
		now = xnclock_read_raw();
		base = hostrt_data->cycle_last;
		mask = hostrt_data->mask;
		mult = hostrt_data->mult;
		shift = hostrt_data->shift;
		tp->tv_sec = hostrt_data->wall_time_sec;
		nsec = hostrt_data->wall_time_nsec;
	}

	/*
	 * At this point, we have a consistent copy of the fundamental
	 * data structure - calculate the interval between the current
	 * and base time stamp cycles, and convert the difference
	 * to nanoseconds.
	 */
	cycle_delta = (now - base) & mask;
	nsec += (cycle_delta * mult) >> shift;

	/* Convert to the desired sec, usec representation */
	tp->tv_sec += xnclock_divrem_billion(nsec, &rem);
	tp->tv_nsec = rem;

	return 0;
#else /* CONFIG_XENO_OPT_HOSTRT */
	return -EINVAL;
#endif
}

int cobalt_clock_getres(clockid_t clock_id, struct timespec __user *u_ts)
{
	struct timespec ts;
	int err;

	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		err = 0;
		ns2ts(&ts, 1);
		break;
	default:
		err = -EINVAL;
	}

	if (err == 0 && __xn_safe_copy_to_user(u_ts, &ts, sizeof(ts)))
		return -EFAULT;

	return err;
}

int cobalt_clock_gettime(clockid_t clock_id, struct timespec __user *u_ts)
{
	xnticks_t cpu_time;
	struct timespec ts;
	int err = 0;

	switch (clock_id) {
	case CLOCK_REALTIME:
		ns2ts(&ts, xnclock_read());
		break;

	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		cpu_time = xnclock_read_monotonic();
		ts.tv_sec =
			xnarch_uldivrem(cpu_time, ONE_BILLION, &ts.tv_nsec);
		break;

	case CLOCK_HOST_REALTIME:
		if (do_clock_host_realtime(&ts) != 0)
			err = -EINVAL;
		break;

	default:
		err = -EINVAL;
	}

	if (err == 0 && __xn_safe_copy_to_user(u_ts, &ts, sizeof(*u_ts)))
		return -EFAULT;

	return err;
}

int cobalt_clock_settime(clockid_t clock_id, const struct timespec __user *u_ts)
{
	struct timespec ts;
	xnticks_t now;
	spl_t s;

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	if (clock_id != CLOCK_REALTIME
	    || (unsigned long)ts.tv_nsec >= ONE_BILLION)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);
	now = xnclock_read();
	xnclock_adjust((xnsticks_t) (ts2ns(&ts) - now));
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

int cobalt_clock_nanosleep(clockid_t clock_id, int flags,
			   const struct timespec __user *u_rqt,
			   struct timespec __user *u_rmt)
{
	struct timespec rqt, rmt, *rmtp = NULL;
	xnthread_t *cur;
	xnsticks_t rem;
	int err = 0;
	spl_t s;

	if (u_rmt)
		rmtp = &rmt;

	if (__xn_safe_copy_from_user(&rqt, u_rqt, sizeof(rqt)))
		return -EFAULT;

	if (clock_id != CLOCK_MONOTONIC &&
	    clock_id != CLOCK_MONOTONIC_RAW &&
	    clock_id != CLOCK_REALTIME)
		return -EOPNOTSUPP;

	if ((unsigned long)rqt.tv_nsec >= ONE_BILLION)
		return -EINVAL;

	if (flags & ~TIMER_ABSTIME)
		return -EINVAL;

	cur = xnpod_current_thread();

	xnlock_get_irqsave(&nklock, s);

	xnpod_suspend_thread(cur, XNDELAY, ts2ns(&rqt) + 1,
			     clock_flag(flags, clock_id), NULL);

	if (xnthread_test_info(cur, XNBREAK)) {
		if (flags == 0 && rmtp) {
			rem = xntimer_get_timeout_stopped(&cur->rtimer);
			xnlock_put_irqrestore(&nklock, s);
			ns2ts(rmtp, rem > 1 ? rem : 0);
			if (__xn_safe_copy_to_user(u_rmt, rmtp, sizeof(*u_rmt)))
				return -EFAULT;
		} else
			xnlock_put_irqrestore(&nklock, s);

		return -EINTR;
	}

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*@}*/
