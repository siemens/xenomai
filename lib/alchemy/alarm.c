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
 *
 * @defgroup alchemy_alarm Alarm services.
 * @ingroup alchemy_alarm
 * @ingroup alchemy
 *
 * Alarms are general watchdog timers. Alchemy tasks may create any
 * number of alarms and use them to run a user-defined handler, after
 * a specified initial delay has elapsed. Alarms can be either one
 * shot or periodic; in the latter case, the real-time system
 * automatically reprograms the alarm for the next shot according to a
 * user-defined interval value.
 *
 *@{*/

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "reference.h"
#include "internal.h"
#include "alarm.h"
#include "timer.h"

struct pvcluster alchemy_alarm_table;

static struct alchemy_namegen alarm_namegen = {
	.prefix = "alarm",
	.length = sizeof ((struct alchemy_alarm *)0)->name,
};

static struct alchemy_alarm *get_alchemy_alarm(RT_ALARM *alarm, int *err_r)
{
	struct alchemy_alarm *acb;

	if (bad_pointer(alarm))
		goto bad_handle;

	acb = (struct alchemy_alarm *)alarm->handle;
	if (bad_pointer(acb) || timerobj_lock(&acb->tmobj))
		goto bad_handle;

	if (acb->magic == alarm_magic)
		return acb;
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

/**
 * @fn int rt_alarm_create(RT_ALARM *alarm,const char *name,void (*handler)(void *arg),void *arg)
 * @brief Create an alarm object.
 *
 * This routine creates an object triggering an alarm routine at a
 * specified time in the future. Alarms can be periodic or oneshot,
 * depending on the reload interval value passed to rt_alarm_start().
 *
 * @param alarm The address of an alarm descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * alarm. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created alarm into the object registry.
 *
 * @param handler The address of the routine to call when the alarm
 * expires. This routine is passed the @a arg value.
 *
 * @param arg A user-defined opaque argument passed to the @a handler.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * local pool in order to create the alarm.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered alarm.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Valid calling context:
 *
 * - Regular POSIX threads
 * - Xenomai threads
 *
 * @note Alarms are process-private objects and thus cannot be shared
 * by multiple processes, even if they belong to the same Xenomai
 * session.
 */
int rt_alarm_create(RT_ALARM *alarm, const char *name,
		    void (*handler)(void *arg),
		    void *arg)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	acb = pvmalloc(sizeof(*acb));
	if (acb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = timerobj_init(&acb->tmobj);
	if (ret)
		goto fail;

	alchemy_build_name(acb->name, name, &alarm_namegen);
	acb->handler = handler;
	acb->arg = arg;
	acb->expiries = 0;
	acb->magic = alarm_magic;
	alarm->handle = (uintptr_t)acb;

	if (pvcluster_addobj(&alchemy_alarm_table, acb->name, &acb->cobj)) {
		timerobj_destroy(&acb->tmobj);
		ret = -EEXIST;
		goto fail;
	}

	CANCEL_RESTORE(svc);

	return 0;
fail:
	pvfree(acb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_alarm_delete(RT_ALARM *alarm)
 * @brief Delete an alarm.
 *
 * This routine deletes an alarm object previously created by a call
 * to rt_alarm_create().
 *
 * @param alarm The descriptor address of the deleted alarm.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid alarm descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Valid calling contexts:
 *
 * - Regular POSIX threads
 * - Xenomai threads
 */
int rt_alarm_delete(RT_ALARM *alarm)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	timerobj_destroy(&acb->tmobj);
	pvcluster_delobj(&alchemy_alarm_table, &acb->cobj);
	acb->magic = ~alarm_magic;
	pvfree(acb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_alarm_start(RT_ALARM *alarm,RTIME value,RTIME interval)
 * @brief Start an alarm.
 *
 * This routine programs the trigger date of an alarm object. An alarm
 * can be either periodic or oneshot, depending on the @a interval
 * value.
 *
 * Alarm handlers are always called on behalf of Xenomai's internal
 * timer event routine. Therefore, Xenomai routines which can be
 * called from such handlers are restricted to the set of services
 * available on behalf of an asynchronous context.
 *
 * This service overrides any previous setup of the expiry date and
 * reload interval for the alarm.
 *
 * @param alarm The descriptor address of the started alarm.
 *
 * @param value The relative date of the first expiry, expressed in
 * clock ticks (see note).
 *
 * @param interval The reload value of the alarm. It is a periodic
 * interval value to be used for reprogramming the next alarm shot,
 * expressed in clock ticks (see note). If @a interval is equal to
 * TM_INFINITE, the alarm will not be reloaded after it has expired.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid alarm descriptor.
 *
 * Valid calling context: any.
 *
 * @note Each of the initial @a value and @a interval is interpreted
 * as a multiple of the Alchemy clock resolution (see
 * --alchemy-clock-resolution option, defaults to 1 nanosecond).
 */
int rt_alarm_start(RT_ALARM *alarm,
		   RTIME value, RTIME interval)
{
	struct alchemy_alarm *acb;
	struct itimerspec it;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	clockobj_ticks_to_timeout(&alchemy_clock, value, &it.it_value);
	clockobj_ticks_to_timespec(&alchemy_clock, interval, &it.it_interval);
	ret = timerobj_start(&acb->tmobj, alarm_handler, &it);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_alarm_stop(RT_ALARM *alarm)
 * @brief Stop an alarm.
 *
 * This routine disables an alarm object, preventing any further
 * expiry until it is re-enabled via rt_alarm_start().
 *
 * @param alarm The descriptor address of the stopped alarm.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid alarm descriptor.
 *
 * Valid calling context: any.
 */
int rt_alarm_stop(RT_ALARM *alarm)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	ret = timerobj_stop(&acb->tmobj);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_alarm_inquire(RT_ALARM *alarm, RT_ALARM_INFO *info)
 * @brief Query alarm status.
 *
 * This routine returns the status information about the specified @a
 * alarm.
 *
 * @param alarm The descriptor address of the alarm to get the status
 * of.
 *
 * @return Zero is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid alarm descriptor.
 *
 * Valid calling context: any.
 */
int rt_alarm_inquire(RT_ALARM *alarm, RT_ALARM_INFO *info)
{
	struct alchemy_alarm *acb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	acb = get_alchemy_alarm(alarm, &ret);
	if (acb == NULL)
		goto out;

	strcpy(info->name, acb->name);
	info->expiries = acb->expiries;

	put_alchemy_alarm(acb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/*@}*/
