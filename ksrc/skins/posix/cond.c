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


#include <posix/mutex.h>
#include <posix/cond.h>

#define link2cond(laddr) \
    ((pthread_cond_t *)(((char *)laddr) - offsetof(pthread_cond_t, link)))

static pthread_condattr_t default_cond_attr;

static xnqueue_t pse51_condq;

static void cond_destroy_internal (pthread_cond_t *cond)

{
    removeq(&pse51_condq, &cond->link);
    pse51_mark_deleted(cond);
    /* synchbase wait queue may not be empty only when this function is called
       from pse51_cond_obj_cleanup, hence the absence of xnpod_schedule(). */
    xnsynch_destroy(&cond->synchbase);
}

int pthread_cond_init (pthread_cond_t *cond, const pthread_condattr_t *attr)

{
    xnflags_t synch_flags = XNSYNCH_PRIO | XNSYNCH_NOPIP;
    spl_t s;

    if (!attr)
        attr = &default_cond_attr;

    xnlock_get_irqsave(&nklock, s);

    if (attr->magic != PSE51_COND_ATTR_MAGIC)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    cond->magic = PSE51_COND_MAGIC;
    xnsynch_init(&cond->synchbase, synch_flags);
    inith(&cond->link);
    cond->attr = *attr;
    cond->mutex = NULL;

    appendq(&pse51_condq, &cond->link);

    xnlock_put_irqrestore(&nklock, s);

    return 0;    
}

int pthread_cond_destroy (pthread_cond_t *cond)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if (xnsynch_nsleepers(&cond->synchbase))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EBUSY;
	}

    cond_destroy_internal(cond);
    
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pse51_cond_timedwait_internal(pthread_cond_t *cond,
                                  pthread_mutex_t *mutex,
                                  xnticks_t to)
{
    unsigned count;
    pthread_t cur;
    spl_t s;
    int err;

    if (!cond || !mutex)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    /* If another thread waiting for cond does not use the same mutex */
    if (!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t)
       || (cond->mutex && cond->mutex != mutex))
	{
        err = EINVAL;
        goto unlock_and_return;
	}

    cur = pse51_current_thread();

    if (xnpod_unblockable_p() || !cur)
        {
        err = EPERM;
        goto unlock_and_return;
        }

    err = clock_adjust_timeout(&to, cond->attr.clock);

    if(err)
        goto unlock_and_return;
    
    /* Unlock mutex, with its previous recursive lock count stored
       in "count". */
    if(mutex_save_count(mutex, &count))
        {
        err = EINVAL;
        goto unlock_and_return;
        }

    /* Bind mutex to cond. */
    if (cond->mutex == NULL)
        {
        cond->mutex = mutex;
        ++mutex->condvars;
        }

    /* Wait for another thread to signal the condition. */
    xnsynch_sleep_on(&cond->synchbase, to);

    /* There are four possible wakeup conditions :
       - cond_signal / cond_broadcast, no status bit is set, and the function
         should return 0 ;
       - timeout, the status XNTIMEO is set, and the function should return
         ETIMEDOUT ;
       - pthread_kill, the status bit XNBREAK is set, but ignored, the function
         simply returns EINTR (used only by the user-space interface, replaced
         by 0 anywhere else), causing a wakeup, spurious or not whether
         pthread_cond_signal was called between pthread_kill and the moment
         when xnsynch_sleep_on returned ;
       - pthread_cancel, no status bit is set, but cancellation specific bits are
         set, and tested only once the mutex is reacquired, so that the
         cancellation handler can be called with the mutex locked, as required by
         the specification.
    */

    err = 0;
    
    if (xnthread_test_flags(&cur->threadbase, XNBREAK))
        err = EINTR;
    else if (xnthread_test_flags(&cur->threadbase, XNTIMEO))
        err = ETIMEDOUT;

    /* Unbind mutex and cond, if no other thread is waiting, if the job was not
       already done. */
    if (!xnsynch_nsleepers(&cond->synchbase) && cond->mutex != NULL)
	{
        --mutex->condvars;
        cond->mutex = NULL;
	}

    /* relock mutex */
    mutex_restore_count(mutex, count);

    thread_cancellation_point(cur);

  unlock_and_return:
    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int pthread_cond_wait (pthread_cond_t *cond, pthread_mutex_t *mutex)

{
    int err = pse51_cond_timedwait_internal(cond, mutex, XN_INFINITE);

    return err == EINTR ? 0 : err;
}

int pthread_cond_timedwait (pthread_cond_t *cond,
			    pthread_mutex_t *mutex,
			    const struct timespec *abstime)

{
    int err;

    err = pse51_cond_timedwait_internal(cond, mutex, ts2ticks_ceil(abstime)+1);

    return err == EINTR ? 0 : err;
}

int pthread_cond_signal (pthread_cond_t *cond)

{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if(xnsynch_wakeup_one_sleeper(&cond->synchbase) != NULL)
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_cond_broadcast (pthread_cond_t *cond)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if(xnsynch_flush(&cond->synchbase, 0) == XNSYNCH_RESCHED)
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

void pse51_cond_pkg_init (void)

{
    initq(&pse51_condq);
    pthread_condattr_init(&default_cond_attr);
}

void pse51_cond_pkg_cleanup (void)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while ((holder = getheadq(&pse51_condq)) != NULL)
        {
#ifdef CONFIG_XENO_OPT_DEBUG
        xnprintf("Posix condition variable %p was not destroyed, destroying"
                 " now.\n", link2cond(holder));
#endif /* CONFIG_XENO_OPT_DEBUG */
        cond_destroy_internal(link2cond(holder));
        }

    xnlock_put_irqrestore(&nklock, s);
}

EXPORT_SYMBOL(pthread_cond_init);
EXPORT_SYMBOL(pthread_cond_destroy);
EXPORT_SYMBOL(pthread_cond_wait);
EXPORT_SYMBOL(pthread_cond_timedwait);
EXPORT_SYMBOL(pthread_cond_signal);
EXPORT_SYMBOL(pthread_cond_broadcast);
