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

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "reference.h"
#include "internal.h"
#include "alarm.h"
#include "timer.h"

struct cluster alchemy_alarm_table;

static struct alchemy_namegen alarm_namegen = {
	.prefix = "alarm",
	.length = sizeof ((struct alchemy_alarm *)0)->name,
};

static struct alchemy_alarm *get_alchemy_alarm(RT_ALARM *alarm, int *err_r)
{
	struct alchemy_alarm *acb;

	if (alarm == NULL || ((intptr_t)alarm & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	acb = (struct alchemy_alarm *)alarm->handle;
	if (acb == NULL || ((intptr_t)acb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (acb->magic == ~alarm_magic)
		goto dead_handle;

	if (acb->magic != alarm_magic)
		goto bad_handle;

	if (timerobj_lock(&acb->tmobj))
		goto bad_handle;

	/* Recheck under lock. */
	if (acb->magic == alarm_magic)
		return acb;

dead_handle:
	/* Removed under our feet. */
	*err_r = -EIDRM;
	return NULL;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

static inline void put_alchemy_alarm(struct alchemy_alarm *acb)
{
	timerobj_unlock(&acb->tmobj);
}

static void alarm_handler(struct timerobj *tmobj)
{
	struct alchemy_alarm *acb;

	acb = container_of(tmobj, struct alchemy_alarm, tmobj);
	acb->expiries++;
	acb->handler(acb->arg);
}

int rt_alarm_create(RT_ALARM *alarm, const char *name,
		    void (*handler)(void *arg),
		    void *arg)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	acb = pvmalloc(sizeof(*acb));
	if (acb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	__alchemy_build_name(acb->name, name, &alarm_namegen);

	if (cluster_addobj(&alchemy_alarm_table, acb->name, &acb->cobj)) {
		ret = -EEXIST;
		goto fail_cluster;
	}

	ret = timerobj_init(&acb->tmobj);
	if (ret)
		goto fail_timer;

	acb->handler = handler;
	acb->arg = arg;
	acb->magic = alarm_magic;

	COPPERPLATE_UNPROTECT(svc);

	return 0;
fail_timer:
	cluster_delobj(&alchemy_alarm_table, &acb->cobj);
fail_cluster:
	pvfree(acb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_alarm_delete(RT_ALARM *alarm)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	timerobj_destroy(&acb->tmobj);
	cluster_delobj(&alchemy_alarm_table, &acb->cobj);
	acb->magic = ~alarm_magic;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_alarm_start(RT_ALARM *alarm,
		   RTIME value, RTIME interval)
{
	struct alchemy_alarm *acb;
	struct itimerspec it;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	clockobj_ticks_to_timeout(&alchemy_clock, value, &it.it_value);
	clockobj_ticks_to_timespec(&alchemy_clock, interval, &it.it_interval);
	ret = timerobj_start(&acb->tmobj, alarm_handler, &it);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_alarm_stop(RT_ALARM *alarm)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	ret = timerobj_stop(&acb->tmobj);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_alarm_inquire(RT_ALARM *alarm, RT_ALARM_INFO *info)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	strcpy(info->name, acb->name);
	info->expiries = acb->expiries;

	put_alchemy_alarm(acb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}
