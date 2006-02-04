/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#include "vrtx/task.h"
#include "vrtx/event.h"

static xnqueue_t vrtxeventq;

static int event_destroy_internal(vrtxevent_t *event);

void vrtxevent_init (void) {
    initq(&vrtxeventq);
}

void vrtxevent_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&vrtxeventq)) != NULL)
	event_destroy_internal(link2vrtxevent(holder));
}

static int event_destroy_internal (vrtxevent_t *event)

{
    int s;

    removeq(&vrtxeventq,&event->link);
    vrtx_release_id(event->eventid);
    s = xnsynch_destroy(&event->synchbase);
    vrtx_mark_deleted(event);
    xnfree(event);

    return s;
}

int sc_fcreate (int *perr)
{
    vrtxevent_t *event;
    int eventid;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    event = (vrtxevent_t *)xnmalloc(sizeof(*event));

    if (!event)
	{
	*perr = ER_NOCB;
	return 0;
	}

    eventid = vrtx_alloc_id(event);

    if (eventid < 0)
	{
	*perr = ER_NOCB;
	xnfree(event);
	return 0;
	}

    xnsynch_init(&event->synchbase ,XNSYNCH_PRIO);
    inith(&event->link);
    event->eventid = eventid;
    event->magic = VRTX_EVENT_MAGIC;
    event->events = 0;

    xnlock_get_irqsave(&nklock,s);
    appendq(&vrtxeventq,&event->link);
    xnlock_put_irqrestore(&nklock,s);

    *perr = RET_OK;

    return eventid;
}

void sc_fdelete(int eventid, int opt, int *errp)
{
    vrtxevent_t *event;
    spl_t s;

    if ((opt != 0) && (opt != 1))
	{
	*errp = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    event = (vrtxevent_t *)vrtx_find_object_by_id(eventid);

    if (event == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    if (opt == 0 && /* we look for pending task */
	xnsynch_nsleepers(&event->synchbase) > 0)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_PND;
	return;
	}

    /* forcing delete or no task pending */
    if (event_destroy_internal(event) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);
}

int sc_fpend (int group_id, long timeout, int mask, int opt, int *errp)
{
    vrtxevent_t *evgroup;
    vrtxtask_t *task;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ((opt != 0) && (opt != 1))
	{
	*errp = ER_IIP;
	return 0;
	}

    xnlock_get_irqsave(&nklock,s);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);

    if (evgroup == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return 0;
	}

    *errp = RET_OK;

    if ( ( (opt == 0) && ( (mask & evgroup->events) != 0) ) ||
	 ( (opt == 1) && ( (mask & evgroup->events) == mask) ) )
	{
	xnlock_put_irqrestore(&nklock,s);
	return (mask & evgroup->events);
	}

    task = vrtx_current_task();
    task->waitargs.evgroup.opt = opt;
    task->waitargs.evgroup.mask = mask;

    task->vrtxtcb.TCBSTAT = TBSFLAG;

    if (timeout)
	task->vrtxtcb.TCBSTAT |= TBSDELAY;

    /* xnsynch_sleep_on() called for the current thread automatically
       reschedules. */

    xnsynch_sleep_on(&evgroup->synchbase,timeout);

    if (xnthread_test_flags(&task->threadbase,XNRMID))
	{ /* Timeout */
	*errp = ER_DEL;
	}
    else if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	{ /* Timeout */
	*errp = ER_TMO;
	}

    xnlock_put_irqrestore(&nklock,s);

    return mask;
}

void sc_fpost (int group_id, int mask, int *errp)
{
    xnpholder_t *holder, *nholder;
    vrtxevent_t *evgroup;
    int topt;
    int tmask;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);
    if (evgroup == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    if (evgroup->events & mask)
	{ /* one of the bits was set already */
	*errp = ER_OVF;
	}
    else
	{
	*errp = RET_OK;
	}

    evgroup->events |= mask;

    nholder = getheadpq(xnsynch_wait_queue(&evgroup->synchbase));

    while ((holder = nholder) != NULL)
	{
	vrtxtask_t *task = thread2vrtxtask(link2thread(holder,plink));
	topt = task->waitargs.evgroup.opt;
	tmask = task->waitargs.evgroup.mask;

	if ( ( (topt == 0) && ( (tmask & evgroup->events) != 0) ) ||
	     ( (topt == 1) && ( (tmask & evgroup->events) == mask) ) )
	    {
	    nholder = xnsynch_wakeup_this_sleeper(&evgroup->synchbase,holder);
	    }
	else
	    nholder = nextpq(xnsynch_wait_queue(&evgroup->synchbase),holder);
	}

    xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);
}

int sc_fclear (int group_id, int mask, int *errp)
{
    vrtxevent_t *evgroup;
    int oldevents = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);

    if (evgroup == NULL)
	*errp = ER_ID;
    else
	{
	*errp = RET_OK;
	oldevents = evgroup->events;
	evgroup->events &= ~mask;
	}

    xnlock_put_irqrestore(&nklock,s);

    return oldevents;
}

int sc_finquiry (int group_id, int *errp)
{
    vrtxevent_t *evgroup;
    int mask;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);

    if (evgroup == NULL)
	{
	*errp = ER_ID;
	mask = 0;
	}
    else
	{
	*errp = RET_OK;
	mask = evgroup->events;
	}

    xnlock_put_irqrestore(&nklock,s);

    return mask;
}
