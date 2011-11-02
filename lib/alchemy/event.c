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
#include "event.h"
#include "timer.h"

struct cluster alchemy_event_table;

static struct alchemy_namegen event_namegen = {
	.prefix = "event",
	.length = sizeof ((struct alchemy_event *)0)->name,
};

static struct alchemy_event *get_alchemy_event(RT_EVENT *event,
					       struct syncstate *syns, int *err_r)
{
	struct alchemy_event *evcb;

	if (event == NULL || ((intptr_t)event & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	evcb = mainheap_deref(event->handle, struct alchemy_event);
	if (evcb == NULL || ((intptr_t)evcb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (evcb->magic == ~event_magic)
		goto dead_handle;

	if (evcb->magic != event_magic)
		goto bad_handle;

	if (syncobj_lock(&evcb->sobj, syns))
		goto bad_handle;

	/* Recheck under lock. */
	if (evcb->magic == event_magic)
		return evcb;

dead_handle:
	/* Removed under our feet. */
	*err_r = -EIDRM;
	return NULL;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

static inline void put_alchemy_event(struct alchemy_event *evcb,
				     struct syncstate *syns)
{
	syncobj_unlock(&evcb->sobj, syns);
}

static void event_finalize(struct syncobj *sobj)
{
	struct alchemy_event *evcb;
	evcb = container_of(sobj, struct alchemy_event, sobj);
	xnfree(evcb);
}
fnref_register(libalchemy, event_finalize);

int rt_event_create(RT_EVENT *event, const char *name,
		    unsigned long ivalue, int mode)
{
	struct alchemy_event *evcb;
	struct service svc;
	int sobj_flags = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	evcb = xnmalloc(sizeof(*evcb));
	if (evcb == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	__alchemy_build_name(evcb->name, name, &event_namegen);

	if (cluster_addobj(&alchemy_event_table, evcb->name, &evcb->cobj)) {
		xnfree(evcb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	if (mode & EV_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	evcb->magic = event_magic;
	evcb->value = ivalue;
	evcb->mode = mode;
	syncobj_init(&evcb->sobj, sobj_flags,
		     fnref_put(libalchemy, event_finalize));
	event->handle = mainheap_ref(evcb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

int rt_event_delete(RT_EVENT *event)
{
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	evcb = get_alchemy_event(event, &syns, &ret);
	if (evcb == NULL)
		goto out;

	cluster_delobj(&alchemy_event_table, &evcb->cobj);
	evcb->magic = ~event_magic; /* Prevent further reference. */
	syncobj_destroy(&evcb->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_wait_until(RT_EVENT *event,
			unsigned long mask, unsigned long *mask_r,
			int mode, RTIME timeout)
{
	struct alchemy_event_wait *wait;
	struct timespec ts, *timespec;
	unsigned long bits, testval;
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = get_alchemy_event(event, &syns, &ret);
	if (evcb == NULL)
		goto out;

	if (mask == 0) {
		*mask_r = evcb->value;
		goto done;
	}

	bits = evcb->value & mask;
	testval = mode & EV_ANY ? bits : mask;
	*mask_r = bits;

	if (bits && bits == testval)
		goto done;

	if (timeout == TM_NONBLOCK) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	if (threadobj_async_p()) {
		ret = -EPERM;
		goto done;
	}

	wait = threadobj_alloc_wait(struct alchemy_event_wait);
	if (wait == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	wait->mask = mask;
	wait->mode = mode;

	if (timeout != TM_INFINITE) {
		timespec = &ts;
		clockobj_ticks_to_timespec(&alchemy_clock, timeout, timespec);
	} else
		timespec = NULL;

	ret = syncobj_pend(&evcb->sobj, timespec, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_free_wait(wait);
			goto out;
		}
	} else
		*mask_r = wait->mask;

	threadobj_free_wait(wait);
done:
	put_alchemy_event(evcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_wait(RT_EVENT *event,
		  unsigned long mask, unsigned long *mask_r,
		  int mode, RTIME timeout)
{
	struct service svc;
	ticks_t now;

	if (timeout != TM_INFINITE && timeout != TM_NONBLOCK) {
		COPPERPLATE_PROTECT(svc);
		clockobj_get_time(&alchemy_clock, &now, NULL);
		COPPERPLATE_UNPROTECT(svc);
		timeout += now;
	}

	return rt_event_wait_until(event, mask, mask_r, mode, timeout);
}

int rt_event_signal(RT_EVENT *event, unsigned long mask)
{
	struct alchemy_event_wait *wait;
	struct threadobj *thobj, *tmp;
	unsigned long bits, testval;
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = get_alchemy_event(event, &syns, &ret);
	if (evcb == NULL)
		goto out;

	evcb->value |= mask;

	syncobj_for_each_waiter_safe(&evcb->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		bits = wait->mask & mask;
		testval = wait->mode & EV_ANY ? bits : mask;
		if (bits && bits == testval) {
			wait->mask = bits;
			syncobj_wakeup_waiter(&evcb->sobj, thobj);
		}
	}

	put_alchemy_event(evcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_clear(RT_EVENT *event,
		   unsigned long mask, unsigned long *mask_r)
{
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = get_alchemy_event(event, &syns, &ret);
	if (evcb == NULL)
		goto out;

	if (mask_r)
		*mask_r = evcb->value;

	evcb->value &= ~mask;

	put_alchemy_event(evcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_inquire(RT_EVENT *event, RT_EVENT_INFO *info)
{
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = get_alchemy_event(event, &syns, &ret);
	if (evcb == NULL)
		goto out;

	info->value = evcb->value;
	info->nwaiters = syncobj_pend_count(&evcb->sobj);
	strcpy(info->name, evcb->name);

	put_alchemy_event(evcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}
