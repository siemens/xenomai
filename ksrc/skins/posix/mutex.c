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

#include <xenomai/posix/mutex.h>

#define link2mutex(laddr) \
    ((pthread_mutex_t *)(((char *)laddr) - offsetof(pthread_mutex_t, link)))


static pthread_mutexattr_t default_attr;

static xnqueue_t pse51_mutexq;

static void pse51_mutex_destroy_internal (pthread_mutex_t *mutex)

{
    removeq(&pse51_mutexq, &mutex->link);
    pse51_mark_deleted(mutex);
    /* synchbase wait queue may not be empty only when this function is called
       from pse51_mutex_obj_cleanup, hence the absence of xnpod_schedule(). */
    xnsynch_destroy(&mutex->synchbase);
}

void pse51_mutex_pkg_init (void)

{
    initq(&pse51_mutexq);
    pthread_mutexattr_init(&default_attr);
}

void pse51_mutex_pkg_cleanup (void)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while ((holder = getheadq(&pse51_mutexq)) != NULL)
        {
#ifdef CONFIG_XENO_OPT_DEBUG
        xnprintf("Posix mutex %p was not destroyed, destroying now.\n",
                 link2mutex(holder));
#endif /* CONFIG_XENO_OPT_DEBUG */
	pse51_mutex_destroy_internal(link2mutex(holder));
        }

    xnlock_put_irqrestore(&nklock, s);
}

int pthread_mutex_init (pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)

{
    xnflags_t synch_flags = XNSYNCH_PRIO | XNSYNCH_NOPIP;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!attr)
        attr = &default_attr;

    xnlock_get_irqsave(&nklock, s);

    if (attr->magic != PSE51_MUTEX_ATTR_MAGIC)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    mutex->magic = PSE51_MUTEX_MAGIC;
    mutex->attr = *attr;
    mutex->owner = NULL;
    inith(&mutex->link);

    if (attr->protocol == PTHREAD_PRIO_INHERIT)
        synch_flags |= XNSYNCH_PIP;
    
    xnsynch_init(&mutex->synchbase, synch_flags);
    mutex->count = 0;
    appendq(&pse51_mutexq, &mutex->link);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *mutex)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, pthread_mutex_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if (mutex->count || mutex->condvars)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EBUSY;
	}

    pse51_mutex_destroy_internal(mutex);
    
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pse51_mutex_timedlock_break (pthread_mutex_t *mutex, xnticks_t abs_to)

{
    pthread_t cur = pse51_current_thread();
    int err;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    err = mutex_timedlock_internal(mutex, abs_to);

    if (err == EBUSY)
        switch (mutex->attr.type)
	    {
	    case PTHREAD_MUTEX_NORMAL:
		/* Deadlock. */
		for (;;)
                    {
                    xnticks_t to = abs_to;

                    err = clock_adjust_timeout(&to, CLOCK_REALTIME);

                    if (err)
                        break;
                    
		    xnsynch_sleep_on(&mutex->synchbase, to);

		    if (xnthread_test_flags(&cur->threadbase, XNBREAK))
                        {
                        err = EINTR;
                        break;
                        }

		    if (xnthread_test_flags(&cur->threadbase, XNTIMEO))
                        {
                        err = ETIMEDOUT;
                        break;
                        }

		    if (xnthread_test_flags(&cur->threadbase, XNRMID))
                        {
                        err = EIDRM;
                        break;
                        }
                    }

            break;

        case PTHREAD_MUTEX_ERRORCHECK:

            err = EDEADLK;
            break;

        case PTHREAD_MUTEX_RECURSIVE:

            if (mutex->count == UINT_MAX)
		{
                err = EAGAIN;
                break;
		}
                
            ++mutex->count;
            err = 0;
	    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int pthread_mutex_trylock (pthread_mutex_t *mutex)

{
    pthread_t cur;
    int err;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    cur = pse51_current_thread();

    err = mutex_trylock_internal(mutex, cur);
    
    if (err == EBUSY && mutex->attr.type == PTHREAD_MUTEX_RECURSIVE
        && mutex->owner == cur)
        {
            if (mutex->count == UINT_MAX)
                err = EAGAIN;
            else
                {
                ++mutex->count;
                err = 0;
                }
        }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int pthread_mutex_lock (pthread_mutex_t *mutex)

{
    int err;

    do {
        err = pse51_mutex_timedlock_break(mutex, XN_INFINITE);
    } while(err == EINTR || err == EIDRM);

    return err;
}

int pthread_mutex_timedlock (pthread_mutex_t *mutex, const struct timespec *to)

{
    int err;

    do {
        err = pse51_mutex_timedlock_break(mutex, ts2ticks_ceil(to)+1);
    } while(err == EINTR || err == EIDRM);

    return err;
}

int pthread_mutex_unlock (pthread_mutex_t *mutex)

{
    int err;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    err = mutex_unlock_internal(mutex);

    if (err == EPERM && mutex->attr.type == PTHREAD_MUTEX_RECURSIVE)
	{
        if (mutex->owner == pse51_current_thread() && mutex->count)
	    {
            --mutex->count;
            err = 0;
	    }
	}

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

EXPORT_SYMBOL(pthread_mutex_init);
EXPORT_SYMBOL(pthread_mutex_destroy);
EXPORT_SYMBOL(pthread_mutex_trylock);
EXPORT_SYMBOL(pthread_mutex_lock);
EXPORT_SYMBOL(pthread_mutex_timedlock);
EXPORT_SYMBOL(pthread_mutex_unlock);
