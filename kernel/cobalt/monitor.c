/*
 * @file
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>
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
 * \defgroup cobalt_monitor Synchronization monitor services
 *
 * Synchronization monitor services
 *
 * The Cobalt monitor is a double-wait condition object, serializing
 * accesses through a gate. It behaves like a mutex + two condition
 * variables combo with extended signaling logic. Folding several
 * conditions and the serialization support into a single object
 * performs better on low end hw caches and allows for specific
 * optimizations, compared to using separate general-purpose mutex and
 * condvars. This object is used by the Copperplate interface
 * internally when it runs over the Cobalt core.
 *
 * Threads can wait for some resource(s) to be granted (consumer
 * side), or wait for the available resource(s) to drain (producer
 * side).  Therefore, signals are thread-directed for the grant side,
 * and monitor-directed for the drain side.
 *
 * Typically, a consumer would wait for the GRANT condition to be
 * satisfied, signaling the DRAINED condition when more resources
 * could be made available if the protocol implements output
 * contention (e.g. the write side of a message queue waiting for the
 * consumer to release message slots). Conversely, a producer would
 * wait for the DRAINED condition to be satisfied, issuing GRANT
 * signals once more resources have been made available to the
 * consumer.
 *
 * Implementation-wise, the monitor logic is shared with the Cobalt
 * thread object.
  */
#include <nucleus/sys_ppd.h>
#include "internal.h"
#include "thread.h"
#include "monitor.h"

int cobalt_monitor_init(struct cobalt_monitor_shadow __user *u_monsh,
			int flags)
{
	struct cobalt_monitor_shadow monsh;
	struct cobalt_monitor_data *datp;
	struct cobalt_monitor *mon;
	struct cobalt_kqueues *kq;
	unsigned long datoff;
	struct xnheap *heap;
	int pshared;
	spl_t s;

	if (__xn_safe_copy_from_user(&monsh, u_monsh, sizeof(monsh)))
		return -EFAULT;

	mon = xnmalloc(sizeof(*mon));
	if (mon == NULL)
		return -ENOMEM;

	pshared = (flags & COBALT_MONITOR_SHARED) != 0;
	heap = &xnsys_ppd_get(pshared)->sem_heap;
	datp = xnheap_alloc(heap, sizeof(*datp));
	if (datp == NULL) {
		xnfree(mon);
		return -EAGAIN;
	}

	mon->data = datp;
	xnsynch_init(&mon->gate, XNSYNCH_PIP, &datp->owner);
	xnsynch_init(&mon->drain, XNSYNCH_PRIO, NULL);
	mon->flags = flags;
	mon->magic = COBALT_MONITOR_MAGIC;
	inith(&mon->link);
	initq(&mon->waiters);
	kq = cobalt_kqueues(pshared);
	mon->owningq = kq;

	xnlock_get_irqsave(&nklock, s);
	appendq(&kq->monitorq, &mon->link);
	xnlock_put_irqrestore(&nklock, s);

	datp->flags = 0;
	datoff = xnheap_mapped_offset(heap, datp);
	monsh.flags = flags;
	monsh.monitor = mon;
	monsh.u.data_offset = datoff;

	return __xn_safe_copy_to_user(u_monsh, &monsh, sizeof(*u_monsh));
}

/* nklock held, irqs off */
static int cobalt_monitor_enter_inner(struct cobalt_monitor *mon)
{
	struct xnthread *cur = xnpod_current_thread();
	xnflags_t info;
	int ret = 0;

	if (!cobalt_obj_active(mon, COBALT_MONITOR_MAGIC,
			       struct cobalt_monitor))
		return -EINVAL;

	/*
	 * The monitor might have been exited while we were jumping
	 * there for waiting at the gate, lock atomically and return
	 * if so.
	 *
	 * NOTE: monitors do not support recursive entries.
	 */
	ret = xnsynch_fast_acquire(mon->gate.fastlock, xnthread_handle(cur));
	switch(ret) {
	case 0:
		if (xnthread_test_state(cur, XNWEAK))
			xnthread_inc_rescnt(cur);
		break;
	default:
		/* Nah, we really have to wait. */
		info = xnsynch_acquire(&mon->gate, XN_INFINITE, XN_RELATIVE);
		if (info & XNBREAK)
			return -EINTR;
		if (info)	/* No timeout possible. */
			return -EINVAL;
		break;
	}

	mon->data->flags &= ~(COBALT_MONITOR_SIGNALED|COBALT_MONITOR_BROADCAST);

	return 0;
}

int cobalt_monitor_enter(struct cobalt_monitor_shadow __user *u_monsh)
{
	struct cobalt_monitor *mon = NULL;
	int ret;
	spl_t s;

	__xn_get_user(mon, &u_monsh->monitor);

	xnlock_get_irqsave(&nklock, s);
	ret = cobalt_monitor_enter_inner(mon);
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/* nklock held, irqs off */
static void cobalt_monitor_wakeup(struct cobalt_monitor *mon)
{
	struct cobalt_monitor_data *datp = mon->data;
	struct xnthread *p;
	struct xnholder *h;
	pthread_t tid;
	int bcast;

	/*
	 * Having the GRANT signal pending does not necessarily mean
	 * that somebody is actually waiting for it, so we have to
	 * check both conditions below.
	 */
	bcast = (datp->flags & COBALT_MONITOR_BROADCAST) != 0;
	if ((datp->flags & COBALT_MONITOR_GRANTED) == 0 ||
	    emptyq_p(&mon->waiters))
		goto drain;

	/*
	 * Unblock waiters requesting a grant, either those who
	 * received it only or all of them, depending on the broadcast
	 * bit.
	 *
	 * We update the PENDED flag to inform userland about the
	 * presence of waiters, so that it may decide not to issue any
	 * syscall for exiting the monitor if nobody else is waiting
	 * at the gate.
	 */
	h = getheadq(&mon->waiters);
	while (h) {
		tid = container_of(h, struct cobalt_thread, monitor_link);
		h = nextq(&mon->waiters, h);
		p = &tid->threadbase;
		/*
		 * A thread might receive a grant signal albeit it
		 * does not wait on a monitor, or it might have timed
		 * out before we got there, so we really have to check
		 * that ->wchan does match our sleep queue.
		 */
		if (bcast ||
		    (p->u_window->grant_value && p->wchan == &tid->monitor_synch)) {
			xnsynch_wakeup_this_sleeper(&tid->monitor_synch,
						    &p->plink);
			removeq(&mon->waiters, &tid->monitor_link);
			tid->monitor_queued = 0;
		}
	}
drain:
	/*
	 * Unblock threads waiting for a drain event if that signal is
	 * pending, either one or all, depending on the broadcast
	 * flag.
	 */
	if ((datp->flags & COBALT_MONITOR_DRAINED) != 0 &&
	    xnsynch_pended_p(&mon->drain)) {
		if (bcast)
			xnsynch_flush(&mon->drain, 0);
		else
			xnsynch_wakeup_one_sleeper(&mon->drain);
	}

	if (emptyq_p(&mon->waiters) &&
	    !xnsynch_pended_p(&mon->drain))
		datp->flags &= ~COBALT_MONITOR_PENDED;
}

int cobalt_monitor_wait(struct cobalt_monitor_shadow __user *u_monsh,
			int event, const struct timespec __user *u_ts,
			int __user *u_ret)
{
	pthread_t cur = cobalt_current_thread();
	struct cobalt_monitor *mon = NULL;
	struct cobalt_monitor_data *datp;
	xnticks_t timeout = XN_INFINITE;
	xntmode_t tmode = XN_RELATIVE;
	int ret = 0, opret = 0;
	struct xnsynch *synch;
	struct timespec ts;
	xnflags_t info;
	spl_t s;

	__xn_get_user(mon, &u_monsh->monitor);

	if (u_ts) {
		if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
			return -EFAULT;
		timeout = ts2ns(&ts) + 1;
		tmode = XN_ABSOLUTE;
	}

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(mon, COBALT_MONITOR_MAGIC,
			       struct cobalt_monitor)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * The current thread might have sent signals to the monitor
	 * it wants to sleep on: wake up satisfied waiters before
	 * going to sleep.
	 */
	datp = mon->data;
	if (datp->flags & COBALT_MONITOR_SIGNALED)
		cobalt_monitor_wakeup(mon);

	/* Release the gate prior to waiting, all atomically. */
	xnsynch_release(&mon->gate, &cur->threadbase);

	synch = &cur->monitor_synch;
	if (event & COBALT_MONITOR_WAITDRAIN)
		synch = &mon->drain;
	else {
		cur->threadbase.u_window->grant_value = 0;
		appendq(&mon->waiters, &cur->monitor_link);
		cur->monitor_queued = 1;
	}
	datp->flags |= COBALT_MONITOR_PENDED;

	info = xnsynch_sleep_on(synch, timeout, tmode);
	if (info) {
		if ((info & XNRMID) != 0 ||
		    !cobalt_obj_active(mon, COBALT_MONITOR_MAGIC,
				       struct cobalt_monitor)) {
			ret = -EINVAL;
			goto out;
		}

		if ((event & COBALT_MONITOR_WAITDRAIN) == 0 &&
		    cur->monitor_queued) {
			removeq(&mon->waiters, &cur->monitor_link);
			cur->monitor_queued = 0;
		}

		if (emptyq_p(&mon->waiters) && !xnsynch_pended_p(&mon->drain))
			datp->flags &= ~COBALT_MONITOR_PENDED;

		if (info & XNBREAK)
			opret = -EINTR;
		else if (info & XNTIMEO)
			opret = -ETIMEDOUT;
	}

	ret = cobalt_monitor_enter_inner(mon);
out:
	xnlock_put_irqrestore(&nklock, s);

	__xn_put_user(opret, u_ret);

	return ret;
}

int cobalt_monitor_sync(struct cobalt_monitor_shadow __user *u_monsh)
{
	struct cobalt_monitor *mon = NULL;
	int ret = 0;
	spl_t s;

	__xn_get_user(mon, &u_monsh->monitor);

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(mon, COBALT_MONITOR_MAGIC,
			       struct cobalt_monitor)) {
		ret = -EINVAL;
		goto out;
	}

	if (mon->data->flags & COBALT_MONITOR_SIGNALED) {
		cobalt_monitor_wakeup(mon);
		xnsynch_release(&mon->gate, xnpod_current_thread());
		xnpod_schedule();
		ret = cobalt_monitor_enter_inner(mon);
	}
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_monitor_exit(struct cobalt_monitor_shadow __user *u_monsh)
{
	struct cobalt_monitor *mon = NULL;
	int ret = 0;
	spl_t s;

	__xn_get_user(mon, &u_monsh->monitor);

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(mon, COBALT_MONITOR_MAGIC,
			       struct cobalt_monitor)) {
		ret = -EINVAL;
		goto out;
	}

	if (mon->data->flags & COBALT_MONITOR_SIGNALED)
		cobalt_monitor_wakeup(mon);

	xnsynch_release(&mon->gate, xnpod_current_thread());
	xnpod_schedule();
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static void cobalt_monitor_destroy_inner(struct cobalt_monitor *mon,
					 struct cobalt_kqueues *q)
{
	struct xnheap *heap;
	int pshared;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	removeq(&q->monitorq, &mon->link);
	xnsynch_destroy(&mon->gate);
	xnsynch_destroy(&mon->drain);
	mon->magic = 0;
	xnlock_put_irqrestore(&nklock, s);

	pshared = (mon->flags & COBALT_MONITOR_SHARED) != 0;
	heap = &xnsys_ppd_get(pshared)->sem_heap;
	xnheap_free(heap, mon->data);
	xnfree(mon);
}

int cobalt_monitor_destroy(struct cobalt_monitor_shadow __user *u_monsh)
{
	struct xnthread *cur = xnpod_current_thread();
	struct cobalt_monitor *mon = NULL;
	int ret = 0;
	spl_t s;

	__xn_get_user(mon, &u_monsh->monitor);

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(mon, COBALT_MONITOR_MAGIC,
			       struct cobalt_monitor)) {
		ret = -EINVAL;
		goto fail;
	}

	if (xnsynch_pended_p(&mon->drain) || !emptyq_p(&mon->waiters)) {
		ret = -EBUSY;
		goto fail;
	}

	/*
	 * A monitor must be destroyed by the thread currently holding
	 * its gate lock.
	 */
	if (xnsynch_owner_check(&mon->gate, cur)) {
		ret = -EPERM;
		goto fail;
	}

	xnlock_put_irqrestore(&nklock, s);

	cobalt_monitor_destroy_inner(mon, mon->owningq);

	xnpod_schedule();

	return 0;
 fail:
	xnlock_put_irqrestore(&nklock, s);
	
	return ret;
}

void cobalt_monitorq_cleanup(struct cobalt_kqueues *q)
{
	struct cobalt_monitor *mon;
	struct xnholder *h;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((h = getheadq(&q->monitorq))) {
		xnlock_put_irqrestore(&nklock, s);
		mon = container_of(h, struct cobalt_monitor, link);
		cobalt_monitor_destroy_inner(mon, q);
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_monitor_pkg_init(void)
{
	initq(&cobalt_global_kqueues.monitorq);
}

void cobalt_monitor_pkg_cleanup(void)
{
	cobalt_monitorq_cleanup(&cobalt_global_kqueues);
}
