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

#ifndef _POSIX_MUTEX_H
#define _POSIX_MUTEX_H

#include <posix/internal.h>
#include <posix/thread.h>

typedef struct pse51_mutex {
    xnsynch_t synchbase;
    xnholder_t link;            /* Link in pse51_mutexq */

#define link2mutex(laddr)                                               \
    ((pse51_mutex_t *)(((char *)laddr) - offsetof(pse51_mutex_t, link)))

    pthread_mutexattr_t attr;
    unsigned count;             /* lock count. */
    unsigned condvars;          /* count of condition variables using this
				   mutex. */
} pse51_mutex_t;

void pse51_mutex_pkg_init(void);

void pse51_mutex_pkg_cleanup(void);

/* Interruptible versions of pthread_mutex_*. Exposed for use by syscall.c. */
int pse51_mutex_timedlock_break(struct __shadow_mutex *shadow, xnticks_t to);

/* must be called with nklock locked, interrupts off. */
static inline int mutex_trylock_internal(xnthread_t *cur,
                                         struct __shadow_mutex *shadow)
{
    pse51_mutex_t *mutex = shadow->mutex;

    if (xnpod_unblockable_p())
        return EPERM;

    if (!pse51_obj_active(shadow, PSE51_MUTEX_MAGIC, struct __shadow_mutex))
        return EINVAL;

    if (mutex->count)
        return EBUSY;

    xnsynch_set_owner(&mutex->synchbase, cur);
    mutex->count = 1;
    return 0;
}

/* must be called with nklock locked, interrupts off. */
static inline int mutex_timedlock_internal(xnthread_t *cur,
                                           struct __shadow_mutex *shadow,
                                           xnticks_t abs_to)

{
    int err;

    err = mutex_trylock_internal(cur, shadow);

    if (err == EBUSY)
        {
        pse51_mutex_t *mutex = shadow->mutex;

        if (xnsynch_owner(&mutex->synchbase) != cur)
            do
                {
                xnticks_t to = abs_to;
                
                err = clock_adjust_timeout(&to, CLOCK_REALTIME);
                
                if (err)
                    return err;
                
                xnsynch_sleep_on(&mutex->synchbase, to);
                
                if (xnthread_test_flags(cur, XNBREAK))
                    return EINTR;
                
                if (xnthread_test_flags(cur, XNRMID))
                    return EINVAL;
                
                if (xnthread_test_flags(cur, XNTIMEO))
                    return ETIMEDOUT;
                
                err = mutex_trylock_internal(cur, shadow);
                }
            while (err == EBUSY);
        }

    return err;
}

#endif /* !_POSIX_MUTEX_H */
