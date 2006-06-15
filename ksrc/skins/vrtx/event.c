/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003,2006 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <vrtx/task.h>
#include <vrtx/event.h>

static vrtxidmap_t *vrtx_event_idmap;

static xnqueue_t vrtx_event_q;

static int event_destroy_internal(vrtxevent_t *event)
{
	int s;

	removeq(&vrtx_event_q, &event->link);
	s = xnsynch_destroy(&event->synchbase);
	vrtx_put_id(vrtx_event_idmap, event->evid);
	vrtx_mark_deleted(event);
	xnfree(event);

	return s;
}

int vrtxevent_init(void)
{
	initq(&vrtx_event_q);
	vrtx_event_idmap = vrtx_alloc_idmap(VRTX_MAX_EVENTS, 0);
	return vrtx_event_idmap ? 0 : -ENOMEM;
}

void vrtxevent_cleanup(void)
{
	xnholder_t *holder;

	while ((holder = getheadq(&vrtx_event_q)) != NULL)
		event_destroy_internal(link2vrtxevent(holder));

	vrtx_free_idmap(vrtx_event_idmap);
}

int sc_fcreate(int *errp)
{
	vrtxevent_t *evgroup;
	int evid;
	spl_t s;

	evgroup = (vrtxevent_t *)xnmalloc(sizeof(*evgroup));

	if (evgroup == NULL) {
	      nocb:
		*errp = ER_NOCB;
		return -1;
	}

	evid = vrtx_get_id(vrtx_event_idmap, -1, evgroup);

	if (evid < 0) {
		xnfree(evgroup);
		goto nocb;
	}

	xnsynch_init(&evgroup->synchbase, XNSYNCH_PRIO | XNSYNCH_DREORD);
	inith(&evgroup->link);
	evgroup->evid = evid;
	evgroup->magic = VRTX_EVENT_MAGIC;
	evgroup->events = 0;

	xnlock_get_irqsave(&nklock, s);
	appendq(&vrtx_event_q, &evgroup->link);
	xnlock_put_irqrestore(&nklock, s);

	*errp = RET_OK;

	return evid;
}

void sc_fdelete(int evid, int opt, int *errp)
{
	vrtxevent_t *evgroup;
	spl_t s;

	if (opt & ~1) {
		*errp = ER_IIP;
		return;
	}

	xnlock_get_irqsave(&nklock, s);

	evgroup = (vrtxevent_t *)vrtx_get_object(vrtx_event_idmap, evid);

	if (evgroup == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if (opt == 0 &&		/* we look for pending task */
	    xnsynch_nsleepers(&evgroup->synchbase) > 0) {
		*errp = ER_PND;
		goto unlock_and_exit;
	}

	/* forcing delete or no task pending */
	if (event_destroy_internal(evgroup) == XNSYNCH_RESCHED)
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

int sc_fpend(int evid, long timeout, int mask, int opt, int *errp)
{
	vrtxevent_t *evgroup;
	vrtxtask_t *task;
	int mask_r = 0;
	spl_t s;

	if (opt & ~1) {
		*errp = ER_IIP;
		return 0;
	}

	xnlock_get_irqsave(&nklock, s);

	evgroup = (vrtxevent_t *)vrtx_get_object(vrtx_event_idmap, evid);

	if (evgroup == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if ((opt == 0 && (mask & evgroup->events) != 0) ||
	    (opt == 1 && (mask & evgroup->events) == mask)) {
		mask_r = evgroup->events;
		goto unlock_and_exit;
	}

	if (xnpod_unblockable_p()) {
		*errp = -EPERM;
		goto unlock_and_exit;
	}

	task = vrtx_current_task();
	task->waitargs.evgroup.opt = opt;
	task->waitargs.evgroup.mask = mask;
	task->vrtxtcb.TCBSTAT = TBSFLAG;

	if (timeout)
		task->vrtxtcb.TCBSTAT |= TBSDELAY;

	/* xnsynch_sleep_on() called for the current thread automatically
	   reschedules. */

	xnsynch_sleep_on(&evgroup->synchbase, timeout);

	if (xnthread_test_flags(&task->threadbase, XNBREAK))
		*errp = -EINTR;
	else if (xnthread_test_flags(&task->threadbase, XNRMID))
		*errp = ER_DEL;
	else if (xnthread_test_flags(&task->threadbase, XNTIMEO))
		*errp = ER_TMO;
	else
		mask_r = task->waitargs.evgroup.mask;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return mask_r;
}

void sc_fpost(int evid, int mask, int *errp)
{
	xnpholder_t *holder, *nholder;
	vrtxevent_t *evgroup;
	int topt, tmask;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	evgroup = (vrtxevent_t *)vrtx_get_object(vrtx_event_idmap, evid);

	if (evgroup == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	if (evgroup->events & mask)
		/* Some bits were already set: overflow. */
		*errp = ER_OVF;
	else
		*errp = RET_OK;

	evgroup->events |= mask;

	nholder = getheadpq(xnsynch_wait_queue(&evgroup->synchbase));

	while ((holder = nholder) != NULL) {
		vrtxtask_t *waiter =
		    thread2vrtxtask(link2thread(holder, plink));
		topt = waiter->waitargs.evgroup.opt;
		tmask = waiter->waitargs.evgroup.mask;

		if ((topt == 0 && (tmask & evgroup->events) != 0) ||
		    (topt == 1 && (tmask & evgroup->events) == mask)) {
			/* We want to return the state of the event group as of
			   the time the task is readied. */
			waiter->waitargs.evgroup.mask = evgroup->events;
			nholder =
			    xnsynch_wakeup_this_sleeper(&evgroup->synchbase,
							holder);
		} else
			nholder =
			    nextpq(xnsynch_wait_queue(&evgroup->synchbase),
				   holder);
	}

	xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

int sc_fclear(int evid, int mask, int *errp)
{
	vrtxevent_t *evgroup;
	int mask_r;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	evgroup = (vrtxevent_t *)vrtx_get_object(vrtx_event_idmap, evid);

	if (evgroup == NULL) {
		*errp = ER_ID;
		mask_r = 0;
	} else {
		*errp = RET_OK;
		mask_r = evgroup->events;
		evgroup->events &= ~mask;
	}

	xnlock_put_irqrestore(&nklock, s);

	return mask_r;
}

int sc_finquiry(int evid, int *errp)
{
	vrtxevent_t *evgroup;
	int mask_r;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	evgroup = (vrtxevent_t *)vrtx_get_object(vrtx_event_idmap, evid);

	if (evgroup == NULL) {
		*errp = ER_ID;
		mask_r = 0;
	} else {
		*errp = RET_OK;
		mask_r = evgroup->events;
	}

	xnlock_put_irqrestore(&nklock, s);

	return mask_r;
}
