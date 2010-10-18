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

static struct wind_wd *get_wd_from_id(WDOG_ID wdog_id)
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
	int ret;

	wd = pvmalloc(sizeof(*wd));
	if (wd == NULL)
		goto fail;

	ret = timerobj_init(&wd->tmobj);
	if (ret) {
		pvfree(wd);
	fail:
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return (WDOG_ID)0;
	}

	wd->magic = wd_magic;

	return (WDOG_ID)wd;
}

STATUS wdDelete(WDOG_ID wdog_id)
{
	struct wind_wd *wd = get_wd_from_id(wdog_id);
	int ret;

	if (wd == NULL) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	ret = timerobj_destroy(&wd->tmobj);
	if (ret)
		goto objid_error;

	wd->magic = ~wd_magic;
	pvfree(wd);

	return OK;
}

STATUS wdStart(WDOG_ID wdog_id, int delay, void (*handler)(long), long arg)
{
	struct wind_wd *wd = get_wd_from_id(wdog_id);
	struct itimerspec it;
	int ret;

	if (wd == NULL) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	/*
	 * FIXME: we have a small race window here in case the
	 * watchdog is wiped out while we set the timer up; we would
	 * then write to stale memory.
	 */
	wd->handler = handler;
	wd->arg = arg;

	clockobj_ticks_to_timeout(&wind_clock, delay, &it.it_value);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;

	ret = timerobj_start(&wd->tmobj, watchdog_handler, &it);
	if (ret)
		goto objid_error;

	return OK;
}

STATUS wdCancel(WDOG_ID wdog_id)
{
	struct wind_wd *wd = get_wd_from_id(wdog_id);
	int ret;

	if (wd == NULL) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	ret = timerobj_stop(&wd->tmobj);
	if (ret)
		goto objid_error;

	return OK;
}
