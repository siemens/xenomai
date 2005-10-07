/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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


#include "posix/thread.h"

int clock_getres (clockid_t clock_id, struct timespec *res)

{
    if (clock_id != CLOCK_MONOTONIC && clock_id != CLOCK_REALTIME)
        {
        thread_set_errno(EINVAL);
        return -1;
        }

    if(res)
        ticks2ts(res, 1);

    return 0;
}

int clock_gettime (clockid_t clock_id, struct timespec *tp)

{
    xnticks_t cpu_time;

    switch(clock_id)
        {
        case CLOCK_REALTIME:
            ticks2ts(tp, xnpod_get_time());
            break;

        case CLOCK_MONOTONIC:
            cpu_time = xnpod_get_cpu_time();
            tp->tv_sec = xnarch_uldivrem(cpu_time, ONE_BILLION, &tp->tv_nsec);
            break;

        default:
            thread_set_errno(EINVAL);
            return -1;
        }

    return 0;    
}

int clock_settime(clockid_t clock_id, const struct timespec *tp)

{
    if (clock_id != CLOCK_REALTIME || tp->tv_nsec > ONE_BILLION)
        {
        thread_set_errno(EINVAL);
        return -1;
        }

    thread_set_errno(ENOTSUP);
    return -1;
}

int clock_nanosleep (clockid_t clock_id,
		     int flags,
		     const struct timespec *rqtp,
		     struct timespec *rmtp)
{
    xnticks_t start, timeout;
    pthread_t cur;
    spl_t s;
    int err = 0;

    if (clock_id != CLOCK_MONOTONIC && clock_id != CLOCK_REALTIME)
        return ENOTSUP;
    
    if ((unsigned) rqtp->tv_nsec > ONE_BILLION)
        return EINVAL;

    cur = pse51_current_thread();

    xnlock_get_irqsave(&nklock, s);

    start = clock_get_ticks(clock_id);
    timeout = ts2ticks_ceil(rqtp);

    switch (flags)
	{
	default:
            err = EINVAL;
            goto unlock_and_return;

	case TIMER_ABSTIME:
	    timeout -= start;
            if((xnsticks_t) timeout < 0)
                {
                err = 0;
                goto unlock_and_return;
                }

	    break;

	case 0:
	    break;
	}

    xnpod_suspend_thread(&cur->threadbase, XNDELAY, timeout+1, NULL);

    thread_cancellation_point(cur);
        
    if (xnthread_test_flags(&cur->threadbase, XNBREAK))
	{
        xnlock_put_irqrestore(&nklock, s);

        if (flags == 0 && rmtp)
            {
            xnsticks_t rem  = timeout - clock_get_ticks(clock_id);

            ticks2ts(rmtp, rem > 0 ? rem : 0);
            }

        return EINTR;
	}

  unlock_and_return:
    xnlock_put_irqrestore(&nklock, s);
    
    return err;
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    int err = clock_nanosleep(CLOCK_REALTIME, 0, rqtp, rmtp);

    if(!err)
        return 0;

    thread_set_errno(err);
    return -1;
}

EXPORT_SYMBOL(clock_getres);
EXPORT_SYMBOL(clock_gettime);
EXPORT_SYMBOL(clock_settime);
EXPORT_SYMBOL(clock_nanosleep);
EXPORT_SYMBOL(nanosleep);
