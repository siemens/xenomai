/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
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
 *
 * \ingroup native_timer
 */

/*!
 * \ingroup native
 * \defgroup native_timer Timer management services.
 *
 * Timer-related services allow to control the Xenomai system timer which
 * is used in all timed operations.
 *
 *@{*/

#include <nucleus/pod.h>
#include <native/timer.h>

/*!
 * @fn int rt_timer_inquire(RT_TIMER_INFO *info)
 * @brief Inquire about the timer.
 *
 * Return various information about the status of the system timer.
 *
 * @param info The address of a structure the timer information will
 * be written to.
 *
 * @return This service always returns 0.
 *
 * The information block returns the period and the current system
 * date. The period can have the following values:
 *
 * - TM_UNSET is a special value indicating that the system timer is
 * inactive. A call to rt_timer_set_mode() re-activates it.
 *
 * - TM_ONESHOT is a special value indicating that the timer has been
 * set up in oneshot mode.
 *
 * - Any other period value indicates that the system timer is
 * currently running in periodic mode; it is a count of nanoseconds
 * representing the period of the timer, i.e. the duration of a
 * periodic tick or "jiffy".
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_timer_inquire(RT_TIMER_INFO *info)
{
	RTIME period, tsc;

	period = TM_ONESHOT;
	tsc = xnarch_get_cpu_tsc();
	info->period = period;
	info->tsc = tsc;

	/* In aperiodic mode, our idea of time is the same as the
	   CPU's, and a tick equals a nanosecond. */
	info->date = xnarch_tsc_to_ns(tsc) + nkclock.wallclock_offset;

	return 0;
}

/**
 * @fn void rt_timer_spin(RTIME ns)
 * @brief Busy wait burning CPU cycles.
 *
 * Enter a busy waiting loop for a count of nanoseconds. The precision
 * of this service largely depends on the availability of a time stamp
 * counter on the current CPU.
 *
 * Since this service is usually called with interrupts enabled, the
 * caller might be preempted by other real-time activities, therefore
 * the actual delay might be longer than specified.
 *
 * @param ns The time to wait expressed in nanoseconds.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void rt_timer_spin(RTIME ns)
{
	RTIME etime = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(ns);

	while ((SRTIME)(xnarch_get_cpu_tsc() - etime) < 0)
		cpu_relax();
}

/*@}*/

EXPORT_SYMBOL_GPL(rt_timer_inquire);
EXPORT_SYMBOL_GPL(rt_timer_spin);
