/*
 * @file
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>
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
 * \ingroup cobalt
 * \defgroup cobalt_event Event flag group
 *
 * Event flag group services.
 *
 * An event flag group is a synchronization object represented by a
 * long-word structure; every available bit in such word can be used
 * to map a user-defined event flag.  When a flag is set, the
 * associated event is said to have occurred.
 *
 * Xenomai threads and interrupt handlers can use event flags to
 * signal the occurrence of events to other threads; those threads can
 * either wait for the events to occur in a conjunctive manner (all
 * awaited events must have occurred to wake up), or in a disjunctive
 * way (at least one of the awaited events must have occurred to wake
 * up).
 *
 * We expose this non-POSIX feature through the internal API, as a
 * fast IPC mechanism available to the Copperplate interface.
 */
#include "internal.h"
#include "thread.h"
#include "clock.h"
#include "event.h"
#include <trace/events/cobalt-posix.h>

struct event_wait_context {
	struct xnthread_wait_context wc;
	unsigned long value;
	int mode;
};

int cobalt_event_init(struct cobalt_event_shadow __user *u_event,
		      unsigned long value,
		      int flags)
{
	struct cobalt_event_shadow shadow;
	struct cobalt_event_data *datp;
	int pshared, synflags, ret;
	struct cobalt_event *event;
	struct cobalt_kqueues *kq;
	unsigned long datoff;
	struct xnheap *heap;
	spl_t s;

	trace_cobalt_event_init(u_event, value, flags);

	event = xnmalloc(sizeof(*event));
	if (event == NULL)
		return -ENOMEM;

	pshared = (flags & COBALT_EVENT_SHARED) != 0;
	heap = &xnsys_ppd_get(pshared)->sem_heap;
	datp = xnheap_alloc(heap, sizeof(*datp));
	if (datp == NULL) {
		xnfree(event);
		return -EAGAIN;
	}

	ret = xnregistry_enter_anon(event, &event->handle);
	if (ret) {
		xnheap_free(heap, datp);
		xnfree(event);
		return ret;
	}

	event->data = datp;
	event->flags = flags;
	synflags = (flags & COBALT_EVENT_PRIO) ? XNSYNCH_PRIO : XNSYNCH_FIFO;
	xnsynch_init(&event->synch, synflags, NULL);
	kq = cobalt_kqueues(pshared);
	event->owningq = kq;

	xnlock_get_irqsave(&nklock, s);
	list_add_tail(&event->link, &kq->eventq);
	xnlock_put_irqrestore(&nklock, s);

	event->magic = COBALT_EVENT_MAGIC;

	datp->value = value;
	datp->flags = 0;
	datp->nwaiters = 0;
	datoff = xnheap_mapped_offset(heap, datp);
	shadow.flags = flags;
	shadow.handle = event->handle;
	shadow.u.data_offset = datoff;

	return __xn_safe_copy_to_user(u_event, &shadow, sizeof(*u_event));
}

int cobalt_event_wait(struct cobalt_event_shadow __user *u_event,
		      unsigned long bits,
		      unsigned long __user *u_bits_r,
		      int mode,
		      struct timespec __user *u_ts)
{
	unsigned long rbits = 0, testval;
	xnticks_t timeout = XN_INFINITE;
	struct cobalt_event_data *datp;
	xntmode_t tmode = XN_RELATIVE;
	struct event_wait_context ewc;
	struct cobalt_event *event;
	struct timespec ts;
	xnhandle_t handle;
	int ret = 0, info;
	spl_t s;

	handle = cobalt_get_handle_from_user(&u_event->handle);

	if (u_ts) {
		if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
			return -EFAULT;
		timeout = ts2ns(&ts);
		if (timeout) {
			timeout++;
			tmode = XN_ABSOLUTE;
		} else
			timeout = XN_NONBLOCK;
		trace_cobalt_event_timedwait(u_event, bits, mode, &ts);
	} else
		trace_cobalt_event_wait(u_event, bits, mode);

	xnlock_get_irqsave(&nklock, s);

	event = xnregistry_lookup(handle, NULL);
	if (event == NULL || event->magic != COBALT_EVENT_MAGIC) {
		ret = -EINVAL;
		goto out;
	}

	datp = event->data;

	if (bits == 0) {
		/*
		 * Special case: we don't wait for any event, we only
		 * return the current flag group value.
		 */
		rbits = datp->value;
		goto out;
	}

	datp->flags |= COBALT_EVENT_PENDED;
	rbits = datp->value & bits;
	testval = mode & COBALT_EVENT_ANY ? rbits : datp->value;
	if (rbits && rbits == testval)
		goto done;

	if (timeout == XN_NONBLOCK) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	ewc.value = bits;
	ewc.mode = mode;
	xnthread_prepare_wait(&ewc.wc);
	datp->nwaiters++;
	info = xnsynch_sleep_on(&event->synch, timeout, tmode);
	if (info & XNRMID) {
		ret = -EIDRM;
		goto out;
	}
	if (info & (XNBREAK|XNTIMEO)) {
		datp->nwaiters--;
		ret = (info & XNBREAK) ? -EINTR : -ETIMEDOUT;
	} else
		rbits = ewc.value;
done:
	if (!xnsynch_pended_p(&event->synch))
		datp->flags &= ~COBALT_EVENT_PENDED;
out:
	xnlock_put_irqrestore(&nklock, s);

	if (ret == 0 &&
	    __xn_safe_copy_to_user(u_bits_r, &rbits, sizeof(rbits)))
		return -EFAULT;

	return ret;
}

int cobalt_event_sync(struct cobalt_event_shadow __user *u_event)
{
	unsigned long bits, waitval, testval;
	struct xnthread_wait_context *wc;
	struct cobalt_event_data *datp;
	struct event_wait_context *ewc;
	struct cobalt_event *event;
	struct xnthread *p, *tmp;
	xnhandle_t handle;
	int ret = 0;
	spl_t s;

	handle = cobalt_get_handle_from_user(&u_event->handle);

	xnlock_get_irqsave(&nklock, s);

	event = xnregistry_lookup(handle, NULL);
	if (event == NULL || event->magic != COBALT_EVENT_MAGIC) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Userland has already updated the bitmask, our job is to
	 * wake up any thread which could be satisfied by its current
	 * value.
	 */
	datp = event->data;
	bits = datp->value;

	xnsynch_for_each_sleeper_safe(p, tmp, &event->synch) {
		wc = xnthread_get_wait_context(p);
		ewc = container_of(wc, struct event_wait_context, wc);
		waitval = ewc->value & bits;
		testval = ewc->mode & COBALT_EVENT_ANY ? waitval : ewc->value;
		if (waitval && waitval == testval) {
			datp->nwaiters--;
			ewc->value = waitval;
			xnsynch_wakeup_this_sleeper(&event->synch, p);
		}
	}

	xnsched_run();
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static void cobalt_event_destroy_inner(struct cobalt_event *event,
				       struct cobalt_kqueues *q,
				       spl_t s)
{
	struct xnheap *heap;
	int pshared;

	list_del(&event->link);
	xnsynch_destroy(&event->synch);
	xnregistry_remove(event->handle);
	event->magic = 0;
	pshared = (event->flags & COBALT_EVENT_SHARED) != 0;

	xnlock_put_irqrestore(&nklock, s);
	heap = &xnsys_ppd_get(pshared)->sem_heap;
	xnheap_free(heap, event->data);
	xnfree(event);
	xnlock_get_irqsave(&nklock, s);
}

int cobalt_event_destroy(struct cobalt_event_shadow __user *u_event)
{
	struct cobalt_event *event;
	xnhandle_t handle;
	int ret = 0;
	spl_t s;

	trace_cobalt_event_destroy(u_event);

	handle = cobalt_get_handle_from_user(&u_event->handle);

	xnlock_get_irqsave(&nklock, s);

	event = xnregistry_lookup(handle, NULL);
	if (event == NULL || event->magic != COBALT_EVENT_MAGIC) {
		ret = -EINVAL;
		goto out;
	}

	cobalt_event_destroy_inner(event, event->owningq, s);

	xnsched_run();
out:
	xnlock_put_irqrestore(&nklock, s);
	
	return ret;
}

int cobalt_event_inquire(struct cobalt_event_shadow __user *u_event,
			 struct cobalt_event_info __user *u_info,
			 pid_t __user *u_waitlist,
			 size_t waitsz)
{
	int nrpend = 0, nrwait = 0, nrpids, ret = 0;
	unsigned long pstamp, nstamp = 0;
	struct cobalt_event_info info;
	struct cobalt_event *event;
	pid_t *t = NULL, fbuf[16];
	struct xnthread *thread;
	xnhandle_t handle;
	spl_t s;

	handle = cobalt_get_handle_from_user(&u_event->handle);

	nrpids = waitsz / sizeof(pid_t);

	xnlock_get_irqsave(&nklock, s);

	for (;;) {
		pstamp = nstamp;
		event = xnregistry_lookup(handle, &nstamp);
		if (event == NULL || event->magic != COBALT_EVENT_MAGIC) {
			xnlock_put_irqrestore(&nklock, s);
			return -EINVAL;
		}
		/*
		 * Allocate memory to return the wait list without
		 * holding any lock, then revalidate the handle.
		 */
		if (t == NULL) {
			nrpend = 0;
			if (!xnsynch_pended_p(&event->synch))
				break;
			xnsynch_for_each_sleeper(thread, &event->synch)
				nrpend++;
			if (u_waitlist == NULL)
				break;
			xnlock_put_irqrestore(&nklock, s);
			if (nrpids > nrpend)
				nrpids = nrpend;
			if (nrpend <= ARRAY_SIZE(fbuf))
				t = fbuf; /* Use fast buffer. */
			else {
				t = xnmalloc(nrpend * sizeof(pid_t));
				if (t == NULL)
					return -ENOMEM;
			}
			xnlock_get_irqsave(&nklock, s);
		} else if (pstamp == nstamp)
			break;
		else {
			xnlock_put_irqrestore(&nklock, s);
			if (t != fbuf)
				xnfree(t);
			t = NULL;
			xnlock_get_irqsave(&nklock, s);
		}
	}

	info.flags = event->flags;
	info.value = event->value;
	info.nrwait = nrpend;

	if (xnsynch_pended_p(&event->synch) && u_waitlist != NULL) {
		xnsynch_for_each_sleeper(thread, &event->synch) {
			if (nrwait >= nrpids)
				break;
			t[nrwait++] = xnthread_host_pid(thread);
		}
	}

	xnlock_put_irqrestore(&nklock, s);

	ret = __xn_safe_copy_to_user(u_info, &info, sizeof(info));
	if (ret == 0 && nrwait > 0)
		ret = __xn_safe_copy_to_user(u_waitlist, t, nrwait * sizeof(pid_t));

	if (t && t != fbuf)
		xnfree(t);

	return ret ?: nrwait;
}

void cobalt_eventq_cleanup(struct cobalt_kqueues *q)
{
	struct cobalt_event *event, *tmp;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!list_empty(&q->eventq)) {
		list_for_each_entry_safe(event, tmp, &q->eventq, link)
			cobalt_event_destroy_inner(event, q, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_event_pkg_init(void)
{
	INIT_LIST_HEAD(&cobalt_global_kqueues.eventq);
}

void cobalt_event_pkg_cleanup(void)
{
	cobalt_eventq_cleanup(&cobalt_global_kqueues);
}
