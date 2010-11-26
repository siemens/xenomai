/*
 * Copyright (C) 2008-2010 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * Watchdog support.
 *
 * Not shareable (we can't tell whether the handler would always be
 * available in all processes).
 */

#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <signal.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include <vxworks/errnoLib.h>
#include "wdLib.h"
#include "tickLib.h"

#define wd_magic	0x3a4b5c6d

static struct wind_wd *find_wd_from_id(WDOG_ID wdog_id)
{
	struct wind_wd *wd = (struct wind_wd *)wdog_id;

	if (wd == NULL || ((intptr_t)wd & (sizeof(intptr_t)-1)) != 0 ||
	    wd->magic != wd_magic)
		return NULL;

	return wd;
}

static void watchdog_handler(struct timerobj *tmobj)
{
	struct wind_wd *wd = container_of(tmobj, struct wind_wd, tmobj);
	wd->handler(wd->arg);
}

WDOG_ID wdCreate(void)
{
	struct wind_wd *wd;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	wd = pvmalloc(sizeof(*wd));
	if (wd == NULL)
		goto fail;

	ret = timerobj_init(&wd->tmobj);
	if (ret) {
		pvfree(wd);
	fail:
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		wd = NULL;
		goto out;
	}

	wd->magic = wd_magic;
out:
	COPPERPLATE_UNPROTECT(svc);

	return (WDOG_ID)wd;
}

STATUS wdDelete(WDOG_ID wdog_id)
{
	struct wind_wd *wd;
	struct service svc;
	int ret = OK;

	COPPERPLATE_PROTECT(svc);

	/*
	 * XXX: we don't actually have to protect find_wd_from_id()
	 * since it can't be cancelled while holding a lock and does
	 * not change the system state, but the code looks better when
	 * we do so; besides, this small overhead only hits the error
	 * path.
	 */
	wd = find_wd_from_id(wdog_id);
	if (wd == NULL)
		goto objid_error;

	ret = timerobj_destroy(&wd->tmobj);
	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		ret = ERROR;
		goto out;
	}

	wd->magic = ~wd_magic;
	pvfree(wd);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

STATUS wdStart(WDOG_ID wdog_id, int delay, void (*handler)(long), long arg)
{
	struct itimerspec it;
	struct service svc;
	struct wind_wd *wd;
	int ret;

	wd = find_wd_from_id(wdog_id);
	if (wd == NULL)
		goto objid_error;

	/*
	 * FIXME: we have a small race window here in case the
	 * watchdog is wiped out while we set the timer up; we would
	 * then write to stale memory.
	 */
	wd->handler = handler;
	wd->arg = arg;

	COPPERPLATE_PROTECT(svc);

	clockobj_ticks_to_timeout(&wind_clock, delay, &it.it_value);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;
	ret = timerobj_start(&wd->tmobj, watchdog_handler, &it);

	COPPERPLATE_UNPROTECT(svc);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

STATUS wdCancel(WDOG_ID wdog_id)
{
	struct wind_wd *wd;
	struct service svc;
	int ret;

	wd = find_wd_from_id(wdog_id);
	if (wd == NULL)
		goto objid_error;

	COPPERPLATE_PROTECT(svc);
	ret = timerobj_stop(&wd->tmobj);
	COPPERPLATE_UNPROTECT(svc);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}
