/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
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

#include <xenomai/nucleus/pod.h>
#include <xenomai/rtai/task.h>
#include <xenomai/rtai/timer.h>

static int __rtai_oneshot;

void rt_set_oneshot_mode (void)

{
    xnpod_stop_timer();
    __rtai_oneshot = 1;
}

void rt_set_periodic_mode (void)

{
    xnpod_stop_timer();
    __rtai_oneshot = 0;
}

RTIME start_rt_timer (int period)

{
    /* count2nano() and nano2count() are no-ops, so we should have
       been passed nanoseconds, as xnpod_start_timer() expects. */
    xnpod_start_timer(__rtai_oneshot ? XN_APERIODIC_TICK : period,
		      XNPOD_DEFAULT_TICKHANDLER);
    return period;
}

void stop_rt_timer (void)

{
    xnpod_stop_timer();
}

void rt_sleep (RTIME delay)

{
    if (delay <= 0)
	return;

    xnpod_suspend_thread(&rtai_current_task()->thread_base,
			 XNDELAY,
			 delay,
			 NULL);
}

RTIME rt_get_time_ns(void)
{
	RTIME ret;

	ret = xnpod_get_time();
	
	return __rtai_oneshot ? count2nano(ret) : ret;
}


EXPORT_SYMBOL(rt_set_oneshot_mode);
EXPORT_SYMBOL(rt_set_periodic_mode);
EXPORT_SYMBOL(start_rt_timer);
EXPORT_SYMBOL(stop_rt_timer);
EXPORT_SYMBOL(rt_sleep);
EXPORT_SYMBOL(rt_get_time_ns);
