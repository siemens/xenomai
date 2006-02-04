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

static xnqueue_t vrtxtaskq;

static u_long vrtxstacksz;

static xnticks_t vrtxtslice;

static vrtxtask_t *vrtxtaskmap[VRTX_MAX_TID + 1];

static TCB vrtxidletcb;

static void vrtxtask_delete_hook (xnthread_t *thread)

{
    /* The scheduler is locked while hooks are running */
    vrtxtask_t *task;
    spl_t s;

    if (xnthread_get_magic(thread) != VRTX_SKIN_MAGIC)
	return;

    task = thread2vrtxtask(thread);

    xnlock_get_irqsave(&nklock,s);
    removeq(&vrtxtaskq,&task->link);
    xnlock_put_irqrestore(&nklock,s);

    if (task->param != NULL && task->paramsz > 0)
	xnfree(task->param);

    vrtxtaskmap[task->tid] = NULL;
    vrtx_mark_deleted(task);
    xnfree(task);
}

void vrtxtask_init (u_long stacksize)

{
    initq(&vrtxtaskq);
    vrtxstacksz = stacksize;
    xnpod_add_hook(XNHOOK_THREAD_DELETE,vrtxtask_delete_hook);
}

void vrtxtask_cleanup (void)

{
    xnholder_t *holder;
    int err;

    while ((holder = getheadq(&vrtxtaskq)) != NULL)
	sc_tdelete(link2vrtxtask(holder)->tid,0,&err);

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,vrtxtask_delete_hook);
}

static void vrtxtask_trampoline (void *cookie) {

    vrtxtask_t *task = (vrtxtask_t *)cookie;
    int err;

    task->entry(task->param);
    sc_tdelete(0,0,&err);
}

int sc_tecreate (void (*entry)(void *),
		 int tid,
		 int prio,
		 int mode,
		 u_long user,
		 u_long sys,
		 char *paddr,
		 u_long psize,
		 int *perr)
{
    xnflags_t bmode = 0;
    vrtxtask_t *task;
    char name[16];
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (user == 0)
	user = vrtxstacksz;

    if (prio < 0 || prio > 255 ||
	tid < -1 || tid > 255 ||
	user + sys < 1024)	/* Tiny stack */
	{
	*perr = ER_IIP;
	return -1;
	}

    if (paddr != NULL && psize > 0)
	{
	char *_paddr = xnmalloc(psize);

	if (!_paddr)
	    {
	    *perr = ER_MEM;
	    return -1;
	    }

	memcpy(_paddr,paddr,psize);
	paddr = _paddr;
	}

    xnlock_get_irqsave(&nklock,s);

    if (tid < 0)
	{
	for (tid = 256; tid < VRTX_MAX_TID; tid++)
	    {
	    if (vrtxtaskmap[tid] == NULL)
		break;
	    }

	if (tid >= VRTX_MAX_TID)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *perr = ER_TCB;
	    return -1;
	    }
	}
    else if (tid > 0 && vrtxtaskmap[tid] != NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_TID;
	return -1;
	}

    vrtxtaskmap[tid] = (vrtxtask_t *)1;	/* Reserve slot */

    xnlock_put_irqrestore(&nklock,s);

    task = xnmalloc(sizeof(*task));
    if (!task)
	{
	vrtxtaskmap[tid] = NULL;
	*perr = ER_TCB;
	return -1;
	}

    sprintf(name,"t%.3d",tid);

    if (xnpod_init_thread(&task->threadbase,
			  name,
			  prio,
			  !(mode & 0x8) ? XNFPU : 0,
			  user + sys) != 0)
	{
	vrtxtaskmap[tid] = NULL;
	xnfree(task);
	*perr = ER_MEM;
	return -1;
	}

    xnthread_set_magic(&task->threadbase,VRTX_SKIN_MAGIC);

    inith(&task->link);
    task->tid = tid;
    task->entry = entry;
    task->param = paddr;
    task->paramsz = psize;
    task->magic = VRTX_TASK_MAGIC;
    task->vrtxtcb.TCBSTAT = 0;

    if (mode & 0x2)
	bmode |= XNSUSP;

    if (mode & 0x4)
	bmode |= XNLOCK;

    if (mode & 0x10)
	bmode |= XNRRB;

    *perr = RET_OK;

    xnlock_get_irqsave(&nklock,s);
    vrtxtaskmap[tid] = task;	/* Tid 0 won't be searched anyway */
    appendq(&vrtxtaskq,&task->link);
    xnlock_put_irqrestore(&nklock,s);

    xnpod_start_thread(&task->threadbase,
		       bmode,
		       0,
		       XNPOD_ALL_CPUS,
		       vrtxtask_trampoline,
		       task);
    return tid;
}

int sc_tcreate (void (*entry)(void*),
		int tid,
		int prio,
		int *perr) {

    return sc_tecreate(entry,
		       tid,
		       prio,
		       0x0,
		       vrtxstacksz,
		       0,
		       NULL,
		       0,
		       perr);
}

/*
 * delete_task_internal() -- Attempt to remove a VRTX task from the
 * system.
 */

void vrtxtask_delete_internal (vrtxtask_t *task)

{
    /* FIXME: mutex safety code is missing */
    xnpod_delete_thread(&task->threadbase);
}

/*
 * sc_tdelete() -- Delete a task or a group of tasks.
 * CAVEAT: If the caller belongs to the priority group of the deleted
 * tasks (opt == 'A'), the operation may be suspended somewhere in the
 * middle of the deletion loop and never resume.
 */

void sc_tdelete (int tid, int opt, int *perr)

{
    vrtxtask_t *task;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (opt == 'A') /* Delete by priority (depleted form) */
	{
	xnholder_t *holder, *nextholder;

	xnlock_get_irqsave(&nklock,s);

	nextholder = getheadq(&vrtxtaskq);

	while ((holder = nextholder) != NULL)
	    {
	    nextholder = nextq(&vrtxtaskq,holder);
	    task = link2vrtxtask(holder);

	    /* The task base priority is tested here, this excludes
	       temporarily raised priorities due to a PIP boost. */

	    if (xnthread_base_priority(&task->threadbase) == tid)
		vrtxtask_delete_internal(task);
	    }

	xnlock_put_irqrestore(&nklock,s);

	*perr = RET_OK;

	return;
	}

    if (opt != 0)
	{
	*perr = ER_IIP;
	return;
	}

    if (tid < -1 || tid >= VRTX_MAX_TID)
	{
	*perr = ER_TID;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    if (tid == 0)
	task = vrtx_current_task();
    else
	{
	task = vrtxtaskmap[tid];

	if (!task)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *perr = ER_TID;
	    return;
	    }
	}

    vrtxtask_delete_internal(task);

    xnlock_put_irqrestore(&nklock,s);

    *perr = RET_OK;
}

void sc_tpriority (int tid, int prio, int *perr)

{
    vrtxtask_t *task;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (prio < 0 || prio > 255)
	{
	*perr = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    if (tid == 0)
	task = vrtx_current_task();
    else if (tid < -1 || tid >= VRTX_MAX_TID ||
	     (task = vrtxtaskmap[tid]) == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_TID;
	return;
	}

    if (prio == xnthread_base_priority(&task->threadbase))
	{
	/* Allow a round-robin effect if newprio == oldprio... */
	if (!xnthread_test_flags(&task->threadbase,XNBOOST))
	    /* ...unless the thread is PIP-boosted. */
	    xnpod_resume_thread(&task->threadbase,0);
	}
    else
	xnpod_renice_thread(&task->threadbase,prio);

    xnlock_put_irqrestore(&nklock,s);
    
    *perr = RET_OK;

    xnpod_schedule();
}

/*
 * sc_tresume() -- Resume a task or a group of tasks.
 * CAVEAT: If the calling task is targeted as a result of this
 * call, it is not clear whether the operation should lead to an
 * implicit round-robin effect or not. It currently does.
 */

void sc_tresume (int tid, int opt, int *perr)

{
    vrtxtask_t *task;
    spl_t s;

    if (opt == 'A') /* Resume by priority (depleted form) */
	{
	xnholder_t *holder, *nextholder;

	xnlock_get_irqsave(&nklock,s);

	nextholder = getheadq(&vrtxtaskq);

	while ((holder = nextholder) != NULL)
	    {
	    nextholder = nextq(&vrtxtaskq,holder);
	    task = link2vrtxtask(holder);

	    /* The task base priority is tested here, this excludes
	       temporarily raised priorities due to a PIP boost. */

	    if (xnthread_base_priority(&task->threadbase) == tid)
		xnpod_resume_thread(&task->threadbase,XNSUSP);
	    }

	xnlock_put_irqrestore(&nklock,s);

	*perr = RET_OK;

	xnpod_schedule();

	return;
	}

    if (opt != 0)
	{
	*perr = ER_IIP;
	return;
	}

    if (tid < -1 || tid >= VRTX_MAX_TID)
	{
	*perr = ER_TID;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    if (tid == 0)
	task = vrtx_current_task();
    else
	task = vrtxtaskmap[tid];

    if (!task)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_TID;
	return;
	}

    xnpod_resume_thread(&task->threadbase,XNSUSP);

    xnlock_put_irqrestore(&nklock,s);

    *perr = RET_OK;

    xnpod_schedule();
}

/*
 * sc_tsuspend() -- Suspend a task or a group of tasks.
 * CAVEAT: If the caller belongs to the priority group of the deleted
 * tasks (opt == 'A'), the operation may be suspended somewhere in the
 * middle of the suspension loop and resumed later when the caller is
 * unblocked.
 */

void sc_tsuspend (int tid, int opt, int *perr)

{
    vrtxtask_t *task;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (opt == 'A') /* Resume by priority (depleted form) */
	{
	xnholder_t *holder, *nextholder;

	xnlock_get_irqsave(&nklock,s);

	nextholder = getheadq(&vrtxtaskq);

	while ((holder = nextholder) != NULL)
	    {
	    nextholder = nextq(&vrtxtaskq,holder);
	    task = link2vrtxtask(holder);

	    /* The task base priority is tested here, this excludes
	       temporarily raised priorities due to a PIP boost. */

	    if (xnthread_base_priority(&task->threadbase) == tid)
		{
		task->vrtxtcb.TCBSTAT = TBSSUSP;

		xnpod_suspend_thread(&task->threadbase,
				     XNSUSP,
				     XN_INFINITE,
				     NULL);
		}
	    }

	xnlock_put_irqrestore(&nklock,s);

	*perr = RET_OK;

	return;
	}

    if (opt != 0)
	{
	*perr = ER_IIP;
	return;
	}

    if (tid < -1 || tid >= VRTX_MAX_TID)
	{
	*perr = ER_TID;
	return;
	}


    if (tid == 0)
	{
	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSSUSP;
	*perr = RET_OK;
	xnpod_suspend_self();
	return;
	}
    else
	{
	xnlock_get_irqsave(&nklock,s);

	task = vrtxtaskmap[tid];

	if (!task)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *perr = ER_TID;
	    return;
	    }
	task->vrtxtcb.TCBSTAT = TBSSUSP;
	*perr = RET_OK;

	xnpod_suspend_thread(&task->threadbase,
			     XNSUSP,
			     XN_INFINITE,
			     NULL);

	*perr = RET_OK;

	xnlock_put_irqrestore(&nklock,s);
	}
}

void sc_tslice (u_short ticks)

{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    vrtxtslice = ticks;

    if (ticks == 0)
	xnpod_deactivate_rr();
    else
	xnpod_activate_rr(ticks);
}

void sc_lock (void) {

    xnpod_lock_sched();
}

void sc_unlock (void) {

    xnpod_unlock_sched();
}

TCB *sc_tinquiry (int *pinfo, int tid, int *perr)

{
    vrtxtask_t *task;
    TCB *tcb;
    spl_t s;

    if (tid < -1 || tid >= VRTX_MAX_TID)
	{
	*perr = ER_TID;
	return NULL;
	}

    xnlock_get_irqsave(&nklock,s);

    if (tid == 0)
	task = vrtx_current_task();
    else
	{
	task = vrtxtaskmap[tid];

	if (!task)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *perr = ER_TID;
	    return NULL;
	    }
	}

    if (xnpod_interrupt_p())	/* Called on behalf an ISR */
	{
	pinfo[0] = 0;
	pinfo[1] = 256;
	pinfo[2] = xnpod_idle_p() ? TBSIDLE : 0;
	tcb = &vrtxidletcb;
	tcb->TCBSTAT = pinfo[2];
	}
    else
	{
	tcb = &task->vrtxtcb;

	/* the vrtx specs says that TCB is only valid in a call to */
	/* sc_tinquiry.                                            */
	/* we can set TCBSTAT only before each suspending call     */
	/* and correct it here if the task has been resumed        */
	if (!(testbits(task->threadbase.status, XNTHREAD_BLOCK_BITS)))
	    tcb->TCBSTAT = 0;

	pinfo[0] = task->tid;
	pinfo[1] = xnthread_base_priority(&task->threadbase);
	pinfo[2] = tcb->TCBSTAT;
	}

    xnlock_put_irqrestore(&nklock,s);

    *perr = RET_OK;

    return tcb;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently
 * allowed to scan the global vrtx task queue (vrtxtaskq).
 */
