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

/**
 * @ingroup posix
 * @defgroup posix_thread Threads management services.
 *
 * Threads management services.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_09.html#tag_02_09">
 * Specification.</a>
 * 
 *@{*/

#include <posix/thread.h>
#include <posix/cancel.h>
#include <posix/timer.h>
#include <posix/tsd.h>
#include <posix/sig.h>

xnticks_t pse51_time_slice;

xnqueue_t pse51_threadq;

static pthread_attr_t default_attr;

static void thread_destroy (pthread_t thread)

{
    removeq(&pse51_threadq, &thread->link);
    /* join_sync wait queue may not be empty only when this function is called
       from pse51_thread_pkg_cleanup, hence the absence of xnpod_schedule(). */
    xnsynch_destroy(&thread->join_synch);
    xnfree(thread);
}

static void thread_trampoline (void *cookie)
{
    pthread_t thread = (pthread_t) cookie;
    pthread_exit(thread->entry(thread->arg));
}

static void thread_delete_hook (xnthread_t *xnthread)

{
    pthread_t thread = thread2pthread(xnthread);
    spl_t s;

    if (!thread)
        return;

    xnlock_get_irqsave(&nklock, s);

    pse51_cancel_cleanup_thread(thread);
    pse51_tsd_cleanup_thread(thread);
    pse51_mark_deleted(thread);
    pse51_signal_cleanup_thread(thread);
    pse51_timer_cleanup_thread(thread);

    switch (thread_getdetachstate(thread))
	{
	case PTHREAD_CREATE_DETACHED:

	    thread_destroy(thread);
	    break;

	case PTHREAD_CREATE_JOINABLE:

            xnsynch_wakeup_one_sleeper(&thread->join_synch);
            /* Do not call xnpod_schedule here, this thread will be dead soon,
               so that xnpod_schedule will be called anyway. The TCB will be
               freed by the last joiner. */
	    break;

	default:

	    break;
	}

    xnlock_put_irqrestore(&nklock, s);
}

/**
 * Create a thread.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_create.html">
 * Specification.</a>
 * 
 */
int pthread_create (pthread_t *tid,
		    const pthread_attr_t *attr,
		    void *(*start) (void *),
		    void *arg)
{
    pthread_t thread, cur;
    xnflags_t flags = 0;
    size_t stacksize;
    const char *name;
    int prio;
    spl_t s;

    if (attr && attr->magic != PSE51_THREAD_ATTR_MAGIC)
        return EINVAL;

    thread = (pthread_t)xnmalloc(sizeof(*thread));

    if (!thread)
	return EAGAIN;

    thread->attr = attr ? *attr : default_attr;

    cur = pse51_current_thread();

    if (thread->attr.inheritsched == PTHREAD_INHERIT_SCHED)
	{
        /* cur may be NULL if pthread_create is not called by a pse51
           thread, in which case trying to inherit scheduling
           parameters is treated as an error. */

        if (!cur)
	    {
            xnfree(thread);
            return EINVAL;
	    }

        thread->attr.policy = cur->attr.policy;
        thread->attr.schedparam = cur->attr.schedparam;
	}

    prio = thread->attr.schedparam.sched_priority;
    stacksize = thread->attr.stacksize;
    name = thread->attr.name;
    
    if (thread->attr.fp)
        flags |= XNFPU;

    if (!start)
	flags |= XNSHADOW;	/* Note: no interrupt shield. */
    
    if (xnpod_init_thread(&thread->threadbase,
			  name,
			  prio,
			  flags,
                          stacksize) != 0)
	{
	xnfree(thread);
	return EAGAIN;
	}

    xnthread_set_magic(&thread->threadbase,PSE51_SKIN_MAGIC);
    
    thread->attr.name = xnthread_name(&thread->threadbase);
    
    inith(&thread->link);
    
    thread->magic = PSE51_THREAD_MAGIC;
    thread->entry = start;
    thread->arg = arg;
    xnsynch_init(&thread->join_synch, XNSYNCH_PRIO);

    pse51_cancel_init_thread(thread);
    pse51_signal_init_thread(thread, cur);
    pse51_tsd_init_thread(thread);
    pse51_timer_init_thread(thread);
    
    if (thread->attr.policy == SCHED_RR)
	{
	xnthread_time_slice(&thread->threadbase) = pse51_time_slice;
        flags = XNRRB;
	}
    else
        flags = 0;

    xnlock_get_irqsave(&nklock, s);
    appendq(&pse51_threadq,&thread->link);
    xnlock_put_irqrestore(&nklock, s);

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    thread->hkey.u_tid = 0;
    thread->hkey.mm = NULL;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    
    *tid = thread; /* Must be done before the thread is started. */

    if (start)	/* Do not start shadow threads (i.e. start == NULL). */
	xnpod_start_thread(&thread->threadbase,
			   flags,
			   0,
			   thread->attr.affinity,
			   thread_trampoline,
			   thread);
    return 0;
}

/**
 * Detach a running thread.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_detach.html">
 * Specification.</a>
 * 
 */
int pthread_detach (pthread_t thread)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);

    if (xnsynch_flush(&thread->join_synch,
                      PSE51_JOINED_DETACHED) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

/**
 * Compare thread descriptors.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_equal.html">
 * Specification.</a>
 * 
 */
int pthread_equal (pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

/**
 * Terminate the current thread.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_exit.html">
 * Specification.</a>
 * 
 */
void pthread_exit (void *value_ptr)

{
    pthread_t cur;
    spl_t s;

    cur = pse51_current_thread();

    if (!cur)
        return;

    xnlock_get_irqsave(&nklock, s);
    pse51_thread_abort(cur, value_ptr);
}

/**
 * Wait for termination of a specified thread.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_join.html">
 * Specification.</a>
 * 
 */
int pthread_join (pthread_t thread, void **value_ptr)

{
    int is_last_joiner;
    xnthread_t *cur;
    spl_t s;
    
    cur = xnpod_current_thread();

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)
	&& !pse51_obj_deleted(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (&thread->threadbase == cur)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EDEADLK;
	}

    if (thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    is_last_joiner = 1;
    while (pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        if (xnpod_unblockable_p())
            {
            xnlock_put_irqrestore(&nklock, s);
            return EPERM;
            }

        thread_cancellation_point(cur);

        xnsynch_sleep_on(&thread->join_synch, XN_INFINITE);

        is_last_joiner = xnsynch_wakeup_one_sleeper(&thread->join_synch) == NULL;

        thread_cancellation_point(cur);

        /* In case another thread called pthread_detach. */
        if (xnthread_test_flags(cur, PSE51_JOINED_DETACHED))
	    {
            xnlock_put_irqrestore(&nklock, s);
            return EINVAL;
	    }
	}

    /* If we reach this point, at least one joiner is going to succeed, we can
       mark the joined thread as detached. */
    thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);

    if (value_ptr)
        *value_ptr = thread_exit_status(thread);

    if(is_last_joiner)
        thread_destroy(thread);
    else
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

/**
 * Get descriptor of the calling thread.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_self.html">
 * Specification.</a>
 * 
 */
pthread_t pthread_self (void)

{
    return pse51_current_thread();
}

/**
 * Make a thread periodic.
 */
int pthread_make_periodic_np (pthread_t thread,
                              struct timespec *starttp,
                              struct timespec *periodtp)

{

    xnticks_t start, period;
    int err;
    spl_t s;

    if (!periodtp || !starttp)
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        err = ESRCH;
        goto unlock_and_exit;
        }

    start = ts2ticks_ceil(starttp);
    period = ts2ticks_ceil(periodtp);
    err = -xnpod_set_thread_periodic(&thread->threadbase, start, period);
    
 unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

/**
 * Wait for current thread next period.
 */
int pthread_wait_np(unsigned long *overruns_r)
{
    if (xnpod_unblockable_p())
        return EPERM;

    return -xnpod_wait_thread_period(overruns_r);
}

void pse51_thread_abort (pthread_t thread, void *status)

{
    thread_exit_status(thread) = status;
    thread_setcancelstate(thread, PTHREAD_CANCEL_DISABLE);
    thread_setcanceltype(thread, PTHREAD_CANCEL_DEFERRED);
    xnpod_delete_thread(&thread->threadbase);
}

void pse51_thread_pkg_init (u_long rrperiod)

{
    initq(&pse51_threadq);
    pthread_attr_init(&default_attr);
    pse51_time_slice = rrperiod;
    xnpod_add_hook(XNHOOK_THREAD_DELETE,thread_delete_hook);
}

void pse51_thread_pkg_cleanup (void)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while ((holder = getheadq(&pse51_threadq)) != NULL)
	{
        pthread_t thread = link2pthread(holder);

        if (pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	    {
            /* Remaining running thread. */
            thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
            pse51_thread_abort(thread, NULL);
	    }
	else
            {
            /* Remaining TCB (joinable thread, which was never joined). */
#ifdef CONFIG_XENO_OPT_DEBUG
            xnprintf("Posix thread %p(\"%s\") was created joinable, died, but"
                     " was not joined, destroying it now.\n",
                     thread, thread->threadbase.name);
#endif /* CONFIG_XENO_OPT_DEBUG */
            thread_destroy(thread);
            }
	}

    xnlock_put_irqrestore(&nklock, s);

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,thread_delete_hook);
}

/*@}*/

EXPORT_SYMBOL(pthread_create);
EXPORT_SYMBOL(pthread_detach);
EXPORT_SYMBOL(pthread_equal);
EXPORT_SYMBOL(pthread_exit);
EXPORT_SYMBOL(pthread_join);
EXPORT_SYMBOL(pthread_self);
EXPORT_SYMBOL(sched_yield);
EXPORT_SYMBOL(pthread_make_periodic_np);
EXPORT_SYMBOL(pthread_wait_np);

