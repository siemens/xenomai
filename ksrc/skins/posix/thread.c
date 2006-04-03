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
 * This service create a thread. The created thread may be used with all POSIX
 * skin services.
 *
 * The new thread run the @a start routine, with the @a arg argument.
 *
 * The new thread signal mask is inherited from the current thread, if it was
 * also created with pthread_create(), otherwise the new thread signal mask is
 * empty.
 *
 * Other attributes of the new thread depend on the @a attr argument. If
 * @a attr is null, default values for these attributes are used. See @ref
 * posix_threadattr for a definition of thread creation attributes and their
 * default values.
 *
 * Returning from the @a start routine has the same effect as calling
 * pthread_exit() with the return value.
 *
 * @param tid address where the identifier of the new thread will be stored on
 * success;
 *
 * @param attr thread attributes;
 *
 * @param start thread routine;
 *
 * @param arg thread routine argument.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a attr is invalid;
 * - EAGAIN, insufficient memory exists in the system heap to create a new
 *   thread, increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EINVAL, thread attribute @a inheritsched is set to PTHREAD_INHERIT_SCHED
 *   and the calling thread does not belong to the POSIX skin;
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
 * This service detaches a joinable thread. A detached thread is a thread
 * which control block is automatically reclaimed when it terminates. The
 * control block of a joinable thread, on the other hand, is only reclaimed when
 * joined with the service pthread_join().
 *
 * If some threads are currently blocked in the pthread_join() service with @a
 * thread as a target, they are unblocked and pthread_join() returns EINVAL.
 *
 * @param thread target thread.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is an invalid thread identifier;
 * - EINVAL, @a thread is not joinable.
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
 * Compare thread identifiers.
 *
 * This service compare the thread identifiers @a t1 and @a t2. No attempt is
 * made to check the threads for existence. In order to check if a thread
 * exists, the  pthread_kill() service should be used with the signal number 0.
 *
 * @param t1 thread identifier;
 *
 * @param t2 other thread identifier.
 *
 * @return a non zero value if the thread identifiers are equal;
 * @return 0 otherwise. 
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
 * This service terminate the current thread with the return value @a
 * value_ptr. If the current thread is joinable, the return value is returned to
 * any thread joining the current thread with the pthread_join() service.
 *
 * When a thread terminates, cancellation cleanup handlers are executed in the
 * reverse order that they were pushed. Then, thread-specific data destructors
 * are executed.
 *
 * @param value_ptr thread return value.
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
 * This service blocks the calling thread until the thread @a thread terminates
 * or detaches. If @a thread terminates, its return value is stored at the
 * address @a value_ptr. This service may also be used to get the return value
 * of a thread that already terminated but was not joined.
 *
 * This service is a cancelation point for POSIX skin threads: if the calling
 * thread is canceled while blocked in a call to this service, the cancelation
 * request is honored and @a thread remains joinable.
 *
 * Multiple simultaneous calls to pthread_join() specifying the same target
 * thread block all the callers until the target thread terminates.
 *
 * @param thread identifier of the thread to wait for;
 *
 * @param value_ptr address where the target thread return value will be stored
 * on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - EDEADLK, attempting to join the calling thread;
 * - EINVAL, @a thread is detached;
 * - EPERM, the caller context is invalid.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread;
 * - Xenomai user-space thread (switches to primary mode).
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
 * Get the identifier of the calling thread.
 *
 * This service returns the identifier of the calling thread.
 *
 * @return identifier of the calling thread;
 * @return NULL if the calling thread is not a POSIX skin thread.
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
 *
 * This service make the POSIX skin thread @a thread periodic.
 *
 * @param thread thread identifier;
 *
 * @param starttp start time, expressed as an absolute value of the
 * CLOCK_REALTIME clock;
 *
 * @param periodtp period, expressed as a time interval.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - ETIMEDOUT, the start time has already passed.
 */
int pthread_make_periodic_np (pthread_t thread,
                              struct timespec *starttp,
                              struct timespec *periodtp)

{

    xnticks_t start, period;
    int err;
    spl_t s;

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
 *
 * If it is periodic, this service blocks the calling thread until the next
 * period elapses.
 *
 * This service is a cancelation point for POSIX skin threads.
 *
 * @param overruns_r address where the overruns count is returned in case of
 * overrun.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EPERM, the calling context is invalid;
 * - EWOULDBLOCK, the calling thread is not periodic;
 * - EINTR, this service was interrupted by a signal;
 * - ETIMEDOUT, at least one overrun occurred.
 */
int pthread_wait_np(unsigned long *overruns_r)
{
    xnthread_t *cur;
    int err;

    if (xnpod_unblockable_p())
        return EPERM;

    cur = xnpod_current_thread();
    thread_cancellation_point(cur);
    err = -xnpod_wait_thread_period(overruns_r);
    thread_cancellation_point(cur);

    return err;
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

