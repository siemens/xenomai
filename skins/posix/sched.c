/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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
 */

#include <posix/thread.h>

int sched_get_priority_min (int policy)

{
    switch (policy)
	{
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_OTHER:

	    return PSE51_MIN_PRIORITY;

	default:

	    thread_set_errno(EINVAL);
	    return -1;
	}
}

int sched_get_priority_max (int policy)

{
    switch (policy)
	{
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_OTHER:

	    return PSE51_MAX_PRIORITY;

	default:

	    thread_set_errno(EINVAL);
	    return -1;
	}
}

int sched_rr_get_interval (int pid, struct timespec *interval)

{
    /* The only valid pid is 0. */
    if (pid)
	{
        thread_set_errno(ESRCH);
        return -1;
	}

    ticks2ts(interval, pse51_time_slice);

    return 0;
}

int pthread_getschedparam (pthread_t tid, int *pol, struct sched_param *par)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(tid, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    *pol = tid->attr.policy;
    *par = tid->attr.schedparam;
    
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_setschedparam (pthread_t tid, int pol, const struct sched_param *par)

{
    xnflags_t clrmask, setmask;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(tid, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    switch (pol)
	{
	default:

	    xnlock_put_irqrestore(&nklock, s);
	    return EINVAL;

	case SCHED_FIFO:

	    setmask = 0;
	    clrmask = XNRRB;
	    break;

	case SCHED_OTHER:

	    pol = SCHED_RR;

	case SCHED_RR:

	    xnthread_time_slice(&tid->threadbase) = pse51_time_slice;
	    setmask = XNRRB;
	    clrmask = 0;
	}

    if (par->sched_priority < PSE51_MIN_PRIORITY
	|| par->sched_priority > PSE51_MAX_PRIORITY )
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    tid->attr.policy = pol;
    tid->attr.schedparam = *par;

    xnpod_renice_thread(&tid->threadbase, par->sched_priority);
    xnpod_set_thread_mode(&tid->threadbase, clrmask, setmask);

    xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}


EXPORT_SYMBOL(sched_get_priority_min);
EXPORT_SYMBOL(sched_get_priority_max);
EXPORT_SYMBOL(sched_rr_get_interval);
EXPORT_SYMBOL(pthread_getschedparam);
EXPORT_SYMBOL(pthread_setschedparam);
