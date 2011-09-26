/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <copperplate/threadobj.h>
#include "timer.h"

struct clockobj alchemy_clock;

RTIME rt_timer_read(void)
{
	struct service svc;
	ticks_t ticks;

	COPPERPLATE_PROTECT(svc);
	clockobj_get_time(&alchemy_clock, &ticks, NULL);
	COPPERPLATE_UNPROTECT(svc);

	return ticks;
}

RTIME rt_timer_tsc(void)
{
	return clockobj_get_tsc();
}

SRTIME rt_timer_ns2ticks(SRTIME ns)
{
	return clockobj_ns_to_ticks(&alchemy_clock, ns);
}

SRTIME rt_timer_ticks2ns(SRTIME ticks)
{
	return clockobj_ticks_to_ns(&alchemy_clock, ticks);
}

SRTIME rt_timer_ns2tsc(SRTIME ns)
{
	return clockobj_ns_to_tsc(ns);
}

SRTIME rt_timer_tsc2ns(SRTIME tsc)
{
	return clockobj_tsc_to_ns(tsc);
}

int rt_timer_inquire(RT_TIMER_INFO *info)
{
	struct service svc;

	COPPERPLATE_PROTECT(svc);

	info->period = clockobj_get_resolution(&alchemy_clock);
	if (info->period == 1)
		info->period = TM_ONESHOT;

	clockobj_get_time(&alchemy_clock, &info->date, &info->tsc);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

void rt_timer_spin(RTIME ns)
{
	threadobj_spin(ns);
}
