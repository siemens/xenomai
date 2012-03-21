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

DEFINE_LOOKUP_PRIVATE(event, RT_EVENT);

static void event_finalize(struct eventobj *evobj)
{
	struct alchemy_event *evcb = container_of(evobj, struct alchemy_event, evobj);
	/* We should never fail here, so we backtrace. */
	__bt(syncluster_delobj(&alchemy_event_table, &evcb->cobj));
	evcb->magic = ~event_magic;
	xnfree(evcb);
}
fnref_register(libalchemy, event_finalize);

int rt_event_create(RT_EVENT *event, const char *name,
		    unsigned long ivalue, int mode)
{
	int evobj_flags = 0, ret = 0;
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
	if (mode & EV_PRIO)
		evobj_flags = EVOBJ_PRIO;

	ret = eventobj_init(&evcb->evobj, ivalue, evobj_flags,
			    fnref_put(libalchemy, event_finalize));
	if (ret) {
		xnfree(evcb);
		goto out;
	}

	if (syncluster_addobj(&alchemy_event_table, evcb->name, &evcb->cobj)) {
		eventobj_destroy(&evcb->evobj);
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
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	/*
	 * XXX: we rely on copperplate's eventobj to check for event
	 * existence, so we refrain from altering the object memory
	 * until we know it was valid. So the only safe place to
	 * negate the magic tag, deregister from the cluster and
	 * release the memory is in the finalizer routine, which is
	 * only called for valid objects.
	 */
	ret = eventobj_destroy(&evcb->evobj);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_wait_timed(RT_EVENT *event,
			unsigned long mask, unsigned long *mask_r,
			int mode, const struct timespec *abs_timeout)
{
	int evobj_mode = 0, ret = 0;
	struct alchemy_event *evcb;
	struct service svc;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	if (mode & EV_ANY)
		evobj_mode = EVOBJ_ANY;

	ret = eventobj_wait(&evcb->evobj, mask, mask_r,
			    evobj_mode, abs_timeout);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_signal(RT_EVENT *event, unsigned long mask)
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	ret = eventobj_post(&evcb->evobj, mask);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_clear(RT_EVENT *event,
		   unsigned long mask, unsigned long *mask_r)
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	ret = eventobj_clear(&evcb->evobj, mask, mask_r);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_event_inquire(RT_EVENT *event, RT_EVENT_INFO *info)
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	ret = eventobj_inquire(&evcb->evobj, &info->value);
	if (ret < 0)
		goto out;

	strcpy(info->name, evcb->name);
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
