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

static pthread_mutexattr_t default_attr;

static xnqueue_t pse51_mutexq;

static void pse51_mutex_destroy_internal (pse51_mutex_t *mutex)

{
    removeq(&pse51_mutexq, &mutex->link);
    /* synchbase wait queue may not be empty only when this function is called
       from pse51_mutex_obj_cleanup, hence the absence of xnpod_schedule(). */
    xnsynch_destroy(&mutex->synchbase);
    xnfree(mutex);
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

int pthread_mutex_init (pthread_mutex_t *mx, const pthread_mutexattr_t *attr)
{
    struct __shadow_mutex *shadow = &((union __xeno_mutex *) mx)->shadow_mutex;
    xnflags_t synch_flags = XNSYNCH_PRIO | XNSYNCH_NOPIP;
    pse51_mutex_t *mutex;
    spl_t s;
    
    if (!attr)
        attr = &default_attr;

    xnlock_get_irqsave(&nklock, s);

    if (attr->magic != PSE51_MUTEX_ATTR_MAGIC)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if (shadow->magic == PSE51_MUTEX_MAGIC)
        {
        xnholder_t *holder;
        for(holder = getheadq(&pse51_mutexq); holder;
            holder = nextq(&pse51_mutexq, holder))
            if (holder == &shadow->mutex->link)
                {
                /* mutex is already in the queue. */
                xnlock_put_irqrestore(&nklock, s);
                return EBUSY;
                }
        }

    mutex = (pse51_mutex_t *) xnmalloc(sizeof(*mutex));
    if (!mutex)
        {
        xnlock_put_irqrestore(&nklock, s);
        return ENOMEM;
        }

    shadow->magic = PSE51_MUTEX_MAGIC;
    shadow->mutex = mutex;

    if (attr->protocol == PTHREAD_PRIO_INHERIT)
        synch_flags |= XNSYNCH_PIP;
    
    xnsynch_init(&mutex->synchbase, synch_flags);
    inith(&mutex->link);
    mutex->attr = *attr;
    mutex->count = 0;

    appendq(&pse51_mutexq, &mutex->link);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *mx)

{
    struct __shadow_mutex *shadow = &((union __xeno_mutex *) mx)->shadow_mutex;
    pse51_mutex_t *mutex;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(shadow, PSE51_MUTEX_MAGIC, struct __shadow_mutex))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    mutex = shadow->mutex;

    if (mutex->count || mutex->condvars)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EBUSY;
	}

    pse51_mark_deleted(shadow);
    pse51_mutex_destroy_internal(mutex);
    
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pse51_mutex_timedlock_break (struct __shadow_mutex *shadow, xnticks_t abs_to)

{
    xnthread_t *cur = xnpod_current_thread();
    pse51_mutex_t *mutex;
    int err;
    spl_t s;

    if (xnpod_unblockable_p())
        return EPERM;

    xnlock_get_irqsave(&nklock, s);

    err = mutex_timedlock_internal(shadow, abs_to);

    if (err == EBUSY)
        {
        mutex = shadow->mutex;

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

		    if (xnthread_test_flags(cur, XNBREAK))
                        {
                        err = EINTR;
                        break;
                        }

		    if (xnthread_test_flags(cur, XNTIMEO))
                        {
                        err = ETIMEDOUT;
                        break;
                        }

		    if (xnthread_test_flags(cur, XNRMID))
                        {
                        err = EINVAL;
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
        }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int pthread_mutex_trylock (pthread_mutex_t *mx)

{
    struct __shadow_mutex *shadow = &((union __xeno_mutex *) mx)->shadow_mutex;
    xnthread_t *cur = xnpod_current_thread();
    int err;
    spl_t s;
    
    if (xnpod_unblockable_p() || !cur)
        return EPERM;

    xnlock_get_irqsave(&nklock, s);

    err = mutex_trylock_internal(shadow, cur);

    if (err == EBUSY)
        {
        pse51_mutex_t *mutex = shadow->mutex;

        if (mutex->attr.type == PTHREAD_MUTEX_RECURSIVE
            && xnsynch_owner(&mutex->synchbase) == cur)
            {
            if (mutex->count == UINT_MAX)
                err = EAGAIN;
            else
                {
                ++mutex->count;
                err = 0;
                }
            }
        }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int pthread_mutex_lock (pthread_mutex_t *mx)

{
    struct __shadow_mutex *shadow = &((union __xeno_mutex *) mx)->shadow_mutex;
    int err;

    do {
        err = pse51_mutex_timedlock_break(shadow, XN_INFINITE);
    } while(err == EINTR);

    return err;
}

int pthread_mutex_timedlock (pthread_mutex_t *mx, const struct timespec *to)

{
    struct __shadow_mutex *shadow = &((union __xeno_mutex *) mx)->shadow_mutex;
    int err;

    do {
        err = pse51_mutex_timedlock_break(shadow, ts2ticks_ceil(to)+1);
    } while(err == EINTR);

    return err;
}

int pthread_mutex_unlock (pthread_mutex_t *mx)

{
    struct __shadow_mutex *shadow = &((union __xeno_mutex *) mx)->shadow_mutex;
    int err;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    err = mutex_unlock_internal(shadow);

    if (err == EPERM)
        {
        pse51_mutex_t *mutex = shadow->mutex;

        if(mutex->attr.type == PTHREAD_MUTEX_RECURSIVE
           && xnsynch_owner(&mutex->synchbase) == xnpod_current_thread()
           && mutex->count)
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
