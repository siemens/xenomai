/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#define XENO_THREAD_MODULE 1

#include <xenomai/nucleus/pod.h>
#include <xenomai/nucleus/synch.h>
#include <xenomai/nucleus/heap.h>
#include <xenomai/nucleus/thread.h>
#include <xenomai/nucleus/module.h>

static void xnthread_timeout_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;
    __setbits(thread->status,XNTIMEO); /* Interrupts are off. */
    xnpod_resume_thread(thread,XNDELAY);
}

static void xnthread_periodic_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;

    thread->poverrun++;

    if (xnthread_test_flags(thread,XNDELAY)) /* Prevent unwanted round-robin. */
	xnpod_resume_thread(thread,XNDELAY);
}

int xnthread_init (xnthread_t *thread,
		   const char *name,
		   int prio,
		   xnflags_t flags,
		   unsigned stacksize)
{
    xntimer_init(&thread->rtimer,&xnthread_timeout_handler,thread);
    xntimer_set_priority(&thread->rtimer,XNTIMER_HIPRIO);
    xntimer_init(&thread->ptimer,&xnthread_periodic_handler,thread);
    xntimer_set_priority(&thread->ptimer,XNTIMER_HIPRIO);
    thread->poverrun = -1;

    /* Setup the TCB. */

    xnarch_init_tcb(xnthread_archtcb(thread));

    if (!(flags & XNSHADOW) && stacksize > 0)
	{
	/* Align stack on a word boundary */
	stacksize &= ~(sizeof(int) - 1);

	thread->tcb.stackbase = (unsigned long *)xnarch_alloc_stack(stacksize);

	if (!thread->tcb.stackbase)
	    return -ENOMEM;
	}
    else
	thread->tcb.stackbase = NULL;

    thread->tcb.stacksize = stacksize;
    thread->status = flags;
    thread->signals = 0;
    thread->asrmode = 0;
    thread->asrimask = 0;
    thread->asr = XNTHREAD_INVALID_ASR;
    thread->asrlevel = 0;

    thread->iprio = prio;
    thread->bprio = prio;
    thread->cprio = prio;
    thread->rrperiod = XN_INFINITE;
    thread->rrcredit = XN_INFINITE;
    thread->wchan = NULL;
    thread->magic = 0;

#ifdef CONFIG_XENO_OPT_STATS
    thread->stat.psw = 0;
    thread->stat.ssw = 0;
    thread->stat.csw = 0;
    thread->stat.pf = 0;
#endif /* CONFIG_XENO_OPT_STATS */

    /* These will be filled by xnpod_start_thread() */
    thread->imask = 0;
    thread->imode = 0;
    thread->entry = NULL;
    thread->cookie = 0;
    thread->stime = 0;
    thread->extinfo = NULL;

    if (name)
	xnobject_copy_name(thread->name,name);
    else
	snprintf(thread->name,sizeof(thread->name),"%p",thread);

    inith(&thread->glink);
    inith(&thread->slink);
    initph(&thread->rlink);
    initph(&thread->plink);
    initpq(&thread->claimq,xnpod_get_qdir(nkpod),xnpod_get_maxprio(nkpod,0));

    xnarch_init_display_context(thread);

    return 0;
}

void xnthread_cleanup_tcb (xnthread_t *thread)

{
    xnarchtcb_t *tcb = xnthread_archtcb(thread);

    /* Does not wreck the TCB, only releases the held resources. */

    if (tcb->stackbase)
	xnarch_free_stack((void *) tcb->stackbase);

    thread->magic = 0;
}

char *xnthread_symbolic_status (xnflags_t status, char *buf, int size)
{
    static const char labels[] = XNTHREAD_SLABEL_INIT;
    xnflags_t mask;
    int pos, c;
    char *wp;

    for (mask = status & ~XNTHREAD_SPARES, pos = 0, wp = buf;
	 mask != 0 && wp - buf < size - 2; /* 1-letter label + \0 */
	 mask >>= 1, pos++)
	{
	c = labels[pos];

	if (mask & 1)
	    {
	    switch (1 << pos)
		{
		case XNFPU:
		
		    /* Only output the FPU flag for kernel-based
		       threads; Others get the same level of fp
		       support than any user-space tasks on the
		       current platform. */

		    if (status & (XNSHADOW|XNROOT))
			continue;

		    break;

		case XNROOT:

		    c = 'R';	/* Always mark root as runnable. */
		    break;

		case XNDELAY:

		    /* Only report genuine delays here, not timed
		       waits for resources. */

		    if (status & XNPEND)
			continue;

		    break;

		case XNPEND:

		    /* Report timed waits with lowercase symbol. */

		    if (status & XNDELAY)
			c |= 0x20;

		    break;

		default:

		    if (c == '.')
			continue;
		}

	    *wp++ = c;
	    }
	}

    *wp = '\0';

    return buf;
}
