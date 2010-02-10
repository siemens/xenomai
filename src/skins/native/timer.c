/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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
#include <native/syscall.h>
#include <native/timer.h>
#include <asm-generic/xenomai/timeconv.h>

extern int __native_muxid;

int rt_timer_set_mode(RTIME tickval)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_timer_set_mode,
				 &tickval);
}

RTIME rt_timer_read(void)
{
	RTIME now;

	XENOMAI_SKINCALL1(__native_muxid, __native_timer_read, &now);
	return now;
}

RTIME rt_timer_tsc(void)
{
	RTIME tsc;

#ifdef XNARCH_HAVE_NONPRIV_TSC
	tsc = __xn_rdtsc();
#else /* !XNARCH_HAVE_NONPRIV_TSC */
	XENOMAI_SKINCALL1(__native_muxid, __native_timer_tsc, &tsc);
#endif /* XNARCH_HAVE_NONPRIV_TSC */

	return tsc;
}

SRTIME rt_timer_ns2ticks(SRTIME ns)
{
	RTIME ticks;

	XENOMAI_SKINCALL2(__native_muxid, __native_timer_ns2ticks, &ticks, &ns);
	return ticks;
}

SRTIME rt_timer_ticks2ns(SRTIME ticks)
{
	SRTIME ns;

	XENOMAI_SKINCALL2(__native_muxid, __native_timer_ticks2ns, &ns, &ticks);
	return ns;
}

SRTIME rt_timer_ns2tsc(SRTIME ns)
{
	return xnarch_ns_to_tsc(ns);
}

SRTIME rt_timer_tsc2ns(SRTIME ticks)
{
	return xnarch_tsc_to_ns(ticks);
}

int rt_timer_inquire(RT_TIMER_INFO *info)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_timer_inquire, info);
}

void rt_timer_spin(RTIME ns)
{
	XENOMAI_SKINCALL1(__native_muxid, __native_timer_spin, &ns);
}
