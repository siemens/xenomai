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

#ifndef _XENOMAI_ALCHEMY_TIMER_H
#define _XENOMAI_ALCHEMY_TIMER_H

#include <copperplate/clockobj.h>

typedef ticks_t RTIME;

typedef sticks_t SRTIME;

#define TM_INFINITE  0
#define TM_NOW       0
#define TM_ONESHOT   0
#define TM_NONBLOCK  ((RTIME)-1ULL)

typedef struct rt_timer_info {
	RTIME period;
	RTIME date;
	RTIME tsc;
} RT_TIMER_INFO;

#ifdef __cplusplus
extern "C" {
#endif

RTIME rt_timer_read(void);

RTIME rt_timer_tsc(void);

SRTIME rt_timer_ns2ticks(SRTIME ns);

SRTIME rt_timer_ticks2ns(SRTIME ticks);

SRTIME rt_timer_ns2tsc(SRTIME ns);

SRTIME rt_timer_tsc2ns(SRTIME tsc);

int rt_timer_inquire(RT_TIMER_INFO *info);

void rt_timer_spin(RTIME ns);

#ifdef __cplusplus
}
#endif

#endif /* _ALCHEMY_TIMER_H */
