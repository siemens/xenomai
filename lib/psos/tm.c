/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <memory.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include <copperplate/clockobj.h>
#include <psos/psos.h>
#include "task.h"
#include "tm.h"

struct clockobj psos_clock;

struct timespec psos_rrperiod;

#define tm_magic	0x8181fcfc

static struct psos_tm *get_tm_from_id(u_long tmid, int *err_r)
{
	struct psos_tm *tm = (struct psos_tm *)tmid;

	/*
	 * Unlike most other pSOS objects (except partitions), the
	 * timer control block is NOT laid into the main heap, so
	 * don't have to apply mainheap_deref() to convert timer
	 * handles to pointers, but we do a plain cast instead.  (This
	 * said, mainheap_deref() is smart enough to deal with private
	 * pointers, but we just avoid useless overhead).
	 */
	if (tm == NULL || ((uintptr_t)tm & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (tm->magic == tm_magic)
		return tm;

	if (tm->magic == ~tm_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((tm->magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static void postman_handler(struct timerobj *tmobj)
{
	struct psos_tm *tm = container_of(tmobj, struct psos_tm, tmobj);
	ev_send(tm->tid, tm->events);
}

static u_long start_evtimer(u_long events,
			    struct itimerspec *it, u_long *tmid_r)
{
	struct psos_task *current;
	struct service svc;
	struct psos_tm *tm;
	int ret;

	COPPERPLATE_PROTECT(svc);

	tm = pvmalloc(sizeof(*tm));
	if (tm == NULL) {
		ret = ERR_NOTIMERS;
		goto out;
	}

	pvholder_init(&tm->link);
	tm->events = events;
	tm->magic = tm_magic;

	current = get_psos_task_or_self(0, &ret);
	if (current == NULL) {
		pvfree(tm);
		goto out;
	}

	tm->tid = mainheap_ref(current, u_long);
	pvlist_append(&tm->link, &current->timer_list);
	put_psos_task(current);

	*tmid_r = (u_long)tm;

	ret = timerobj_init(&tm->tmobj);
	if (ret) {
	fail:
		pvfree(tm);
		ret = ERR_NOTIMERS;
		goto out;
	}

	ret = timerobj_start(&tm->tmobj, postman_handler, it);
	if (ret)
		goto fail;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

u_long tm_evafter(u_long ticks, u_long events, u_long *tmid_r)
{
	struct itimerspec it;

	clockobj_ticks_to_timeout(&psos_clock, ticks, &it.it_value);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;

	return start_evtimer(events, &it, tmid_r);
}

u_long tm_evevery(u_long ticks, u_long events, u_long *tmid_r)
{
	struct itimerspec it;

	clockobj_ticks_to_timeout(&psos_clock, ticks, &it.it_value);
	clockobj_ticks_to_timespec(&psos_clock, ticks, &it.it_interval);

	return start_evtimer(events, &it, tmid_r);
}

static int date_to_tmstruct(u_long date, u_long time, u_long ticks,
			    struct tm *tm)
{
	if (ticks > clockobj_get_frequency(&psos_clock))
		return ERR_ILLTICKS;

	memset(tm, 0, sizeof(*tm));

	tm->tm_sec = time & 0xff;
	tm->tm_min = (time >> 8) & 0xff;
	tm->tm_hour = time >> 16;
	if (tm->tm_sec > 59 || tm->tm_min > 59 || tm->tm_hour > 23)
		return ERR_ILLTIME;

	/*
	 * XXX: we want the calendar time to use the time(2) epoch,
	 * i.e. 00:00:00 UTC, January 1, 1970.
	 */

	tm->tm_mday = date & 0xff;
	tm->tm_mon = ((date >> 8) & 0xff) - 1;
	tm->tm_year = (date >> 16) - 1900;
	if (tm->tm_mday < 1 || tm->tm_mday > 31 ||
	    tm->tm_mon > 11 || tm->tm_year < 70)
		return ERR_ILLDATE;

	return 0;
}

static int date_to_timespec(u_long date, u_long time, u_long ticks,
			    struct timespec *ts)
{
	struct tm tm;
	int ret;

	ret = date_to_tmstruct(date, time, ticks, &tm);
	if (ret)
		return ret;

	clockobj_caltime_to_timeout(&psos_clock, &tm, ticks, ts);

	return 0;
}

u_long tm_evwhen(u_long date, u_long time, u_long ticks,
		 u_long events, u_long *tmid_r)
{
	struct itimerspec it;
	int ret;

	ret = date_to_timespec(date, time, ticks, &it.it_value);
	if (ret)
		return ret;

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;

	return start_evtimer(events, &it, tmid_r);
}

u_long tm_cancel(u_long tmid)
{
	struct psos_tm *tm;
	int ret;

	tm = get_tm_from_id(tmid, &ret);
	if (tm == NULL)
		return ret;

	timerobj_stop(&tm->tmobj);
	pvlist_remove(&tm->link);
	pvfree(tm);

	return SUCCESS;
}

u_long tm_wkafter(u_long ticks)
{
	struct timespec rqt;

	if (ticks == 0) {
		sched_yield();	/* Manual round-robin. */
		return SUCCESS;
	}

	clockobj_ticks_to_timeout(&psos_clock, ticks, &rqt);
	clock_nanosleep(CLOCK_COPPERPLATE, TIMER_ABSTIME, &rqt, NULL);

	return SUCCESS;
}

u_long tm_wkwhen(u_long date, u_long time, u_long ticks)
{
	struct timespec rqt;
	int ret;

	ret = date_to_timespec(date, time, ticks, &rqt);
	if (ret)
		return ERR_ILLDATE;

	clock_nanosleep(CLOCK_COPPERPLATE, TIMER_ABSTIME, &rqt, NULL);

	return SUCCESS;
}

u_long tm_set(u_long date, u_long time, u_long ticks)
{
	struct service svc;
	struct tm tm;
	ticks_t t;
	int ret;

	ret = date_to_tmstruct(date, time, ticks, &tm);
	if (ret)
		return ret;

	clockobj_caltime_to_ticks(&psos_clock, &tm, ticks, &t);
	COPPERPLATE_PROTECT(svc);
	clockobj_set_date(&psos_clock, t, 0);
	COPPERPLATE_UNPROTECT(svc);

	return SUCCESS;
}

u_long tm_get(u_long *date_r, u_long *time_r, u_long *ticks_r)
{
	struct tm tm;
	ticks_t t;

	clockobj_get_date(&psos_clock, &t, NULL);
	clockobj_ticks_to_caltime(&psos_clock, t, &tm, ticks_r);
	*date_r = ((tm.tm_year + 1900) << 16) | ((tm.tm_mon + 1) << 8) | tm.tm_mday;
	*time_r = (tm.tm_hour << 16) | (tm.tm_min << 8) | tm.tm_sec;

	return SUCCESS;
}
