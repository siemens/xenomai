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

struct syncluster alchemy_event_table;

static struct alchemy_namegen event_namegen = {
	.prefix = "event",
	.length = sizeof ((struct alchemy_event *)0)->name,
};

DEFINE_SYNC_LOOKUP(event, RT_EVENT);

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
	int sobj_flags = 0, ret = 0;
	struct alchemy_event *evcb;
	struct service svc;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	evcb = xnmalloc(sizeof(*evcb));
	if (evcb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	alchemy_build_name(evcb->name, name, &event_namegen);
	evcb->magic = event_magic;
	evcb->value = ivalue;
	evcb->mode = mode;
	if (mode & EV_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	syncobj_init(&evcb->sobj, sobj_flags,
		     fnref_put(libalchemy, event_finalize));

	if (syncluster_addobj(&alchemy_event_table, evcb->name, &evcb->cobj)) {
		syncobj_uninit(&evcb->sobj);
		xnfree(evcb);
		ret = -EEXIST;
	} else
		event->handle = mainheap_ref(evcb, uintptr_t);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_delete(RT_EVENT *event)
{
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	evcb = get_alchemy_event(event, &syns, &ret);
	if (evcb == NULL)
		goto out;

	syncluster_delobj(&alchemy_event_table, &evcb->cobj);
	evcb->magic = ~event_magic; /* Prevent further reference. */
	syncobj_destroy(&evcb->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_wait_timed(RT_EVENT *event,
			unsigned long mask, unsigned long *mask_r,
			int mode, const struct timespec *abs_timeout)
{
	struct alchemy_event_wait *wait;
	unsigned long bits, testval;
	struct alchemy_event *evcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

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

	if (alchemy_poll_mode(abs_timeout)) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	wait = threadobj_prepare_wait(struct alchemy_event_wait);
	wait->mask = mask;
	wait->mode = mode;

	ret = syncobj_pend(&evcb->sobj, abs_timeout, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_finish_wait();
			goto out;
		}
	} else
		*mask_r = wait->mask;

	threadobj_finish_wait();
done:
	put_alchemy_event(evcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
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

	if (!syncobj_pended_p(&evcb->sobj))
		goto done;

	syncobj_for_each_waiter_safe(&evcb->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		bits = wait->mask & mask;
		testval = wait->mode & EV_ANY ? bits : mask;
		if (bits && bits == testval) {
			wait->mask = bits;
			syncobj_wakeup_waiter(&evcb->sobj, thobj);
		}
	}
done:
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

int rt_event_bind(RT_EVENT *event,
		  const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_event_table,
				   timeout,
				   offsetof(struct alchemy_event, cobj),
				   &event->handle);
}

int rt_event_unbind(RT_EVENT *event)
{
	event->handle = 0;
	return 0;
}
