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
	ticks_t ticks;

	clockobj_get_time(&alchemy_clock, &ticks, NULL);

	return ticks;
}

SRTIME rt_timer_ns2ticks(SRTIME ns)
{
	return clockobj_ns_to_ticks(&alchemy_clock, ns);
}

SRTIME rt_timer_ticks2ns(SRTIME ticks)
{
	return clockobj_ticks_to_ns(&alchemy_clock, ticks);
}

int rt_timer_inquire(RT_TIMER_INFO *info)
{
	info->period = clockobj_get_resolution(&alchemy_clock);
	clockobj_get_time(&alchemy_clock, &info->date, &info->tsc);

	return 0;
}

void rt_timer_spin(RTIME ns)
{
	threadobj_spin(ns);
}
