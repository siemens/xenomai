/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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


#include <xenomai/nucleus/timer.h>
#include <xenomai/posix/thread.h>
#include <xenomai/posix/timer.h>

#define PSE51_TIMER_MAX  128

struct pse51_timer {

    xntimer_t timerbase;

    unsigned queued;
    unsigned overruns;
    unsigned last_overruns;

    xnholder_t link;

#define link2tm(laddr) \
    ((struct pse51_timer *)(((char *)laddr) - offsetof(struct pse51_timer, link)))
    pse51_siginfo_t si;

#define si2tm(saddr) \
    ((struct pse51_timer *)(((char *)saddr) - offsetof(struct pse51_timer, si)))

    clockid_t clockid;
    pthread_t owner;
};


static xnqueue_t timer_freeq;

static struct pse51_timer timer_pool[PSE51_TIMER_MAX];

static void pse51_base_timer_handler (void *cookie)
{
    struct pse51_timer *timer = (struct pse51_timer *)cookie;

    if(timer->queued)
        {
        if(timer->overruns < DELAYTIMER_MAX)
            ++timer->overruns;
        }
    else
        {
        timer->queued = 1;
        timer->overruns = 0;
        pse51_sigqueue_inner(timer->owner, &timer->si);
        }
}

/* Must be called with nklock locked, irq off. */
void pse51_timer_notified (pse51_siginfo_t *si)
{
    struct pse51_timer *timer = si2tm(si);

    timer->queued = 0;
    /* We need this two staged overruns count. The overruns count returned by
       timer_getoverrun is the count of overruns which occured between the time
       the signal was queued and the time this signal was accepted by the
       application.
       In other words, if the timer elapses again after pse51_timer_notified get
       called (i.e. the signal is accepted by the application), the signal shall
       be queued again, and later overruns should count for that new
       notification, not the one the application is currently handling. */
    timer->last_overruns = timer->overruns;
}

int timer_create (clockid_t clockid,
                  const struct sigevent *__restrict__ evp,
                  timer_t *__restrict__ timerid)
{
    struct pse51_timer *timer;
    xnholder_t *holder;
    spl_t s;
    int err;

    if (clockid != CLOCK_MONOTONIC && clockid != CLOCK_REALTIME)
        {
        err = EINVAL;
        goto error;
        }

    /* We only support notification via signals. */
    if (evp && (evp->sigev_notify != SIGEV_SIGNAL ||
                (unsigned) (evp->sigev_signo - 1) > SIGRTMAX - 1))
        {
        err = EINVAL;
        goto error;
        }
    
    xnlock_get_irqsave(&nklock, s);

    holder = getq(&timer_freeq);
    
    if (!holder)
	{
        err = EAGAIN;
        goto unlock_and_error;
	}

    timer = link2tm(holder);

    timer->owner = pse51_current_thread();

    if (evp)
        {
        timer->si.info.si_signo = evp->sigev_signo;
        timer->si.info.si_code = SI_TIMER;
        timer->si.info.si_value = evp->sigev_value;
        }
    else
        {
        timer->si.info.si_signo = SIGALRM;
        timer->si.info.si_code = SI_TIMER;
        timer->si.info.si_value.sival_int = (timer - timer_pool);
        }

    xntimer_init(&timer->timerbase, &pse51_base_timer_handler, timer);

    timer->overruns = 0;

    if (!timer->owner)
        {
        prependq(&timer_freeq, &timer->link);
        err = EPERM;
        goto unlock_and_error;
        }

    inith(&timer->link);
    appendq(&timer->owner->timersq, &timer->link);

    timer->clockid = clockid;
    xnlock_put_irqrestore(&nklock, s);

    *timerid = timer - timer_pool;

    return 0;

  unlock_and_error:
    xnlock_put_irqrestore(&nklock, s);
  error:
    thread_set_errno(err);
    return -1;
}

int timer_delete(timer_t timerid)
{
    struct pse51_timer *timer;
    spl_t s;

    if ((unsigned) timerid >= PSE51_TIMER_MAX)
        goto einval;

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
        goto unlock_and_einval;

    if (timer->queued)
        {
        /* timer signal is queued, unqueue it. */
        pse51_sigunqueue(timer->owner, &timer->si);
        timer->queued = 0;
        }
    
    xntimer_destroy(&timer->timerbase);
    removeq(&timer->owner->timersq, &timer->link);
    timer->owner = NULL;        /* Used for debugging. */
    prependq(&timer_freeq,&timer->link); /* Favour earliest reuse. */

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  unlock_and_einval:
    xnlock_put_irqrestore(&nklock, s);
  einval:
    thread_set_errno(EINVAL);
    return -1;
}

static void pse51_timer_gettime_inner (struct pse51_timer *__restrict__ timer,
                                       struct itimerspec *__restrict__ value)
{
    if (xntimer_running_p(&timer->timerbase))
        {
        ticks2ts(&value->it_value, xntimer_get_timeout(&timer->timerbase));
        ticks2ts(&value->it_interval, xntimer_interval(&timer->timerbase));
        }
    else
        {
        value->it_value.tv_sec = 0;
        value->it_value.tv_nsec = 0;
        value->it_interval.tv_sec = 0;
        value->it_interval.tv_nsec = 0;
        }
}

int timer_settime (timer_t timerid,
                   int flags,
                   const struct itimerspec *__restrict__ value,
                   struct itimerspec *__restrict__ ovalue)
{
    struct pse51_timer *timer;
    spl_t s;

    if ((unsigned) timerid >= PSE51_TIMER_MAX)
        goto einval;

    if ((unsigned) value->it_value.tv_nsec >= ONE_BILLION ||
        ((unsigned) value->it_interval.tv_nsec >= ONE_BILLION &&
         (value->it_value.tv_sec != 0 || value->it_value.tv_nsec != 0)))
        goto einval;

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
        goto unlock_and_einval;

    if (ovalue)
        pse51_timer_gettime_inner(timer, ovalue);    

    if (timer->queued)
        {
        /* timer signal is queued, unqueue it. */
        pse51_sigunqueue(timer->owner, &timer->si);
        timer->queued = 0;
        }

    if (value->it_value.tv_nsec == 0 && value->it_value.tv_sec == 0)
        xntimer_stop(&timer->timerbase);
    else
        {
        xnticks_t start = ts2ticks_ceil(&value->it_value) + 1;
    
        if (flags & TIMER_ABSTIME)
            /* If the initial delay has already passed, the call shall suceed. */
            if (clock_adjust_timeout(&start, timer->clockid))
                /* clock_adjust timeout returns an error if start time has
                   already passed, in which case timer_settime is expected not
                   to return an error but schedule the timer ASAP. */
                /* FIXME: when passing 0 tick, xntimer_start disables the
                   timer, we pass 1.*/
                start = 1;

        xntimer_start(&timer->timerbase,
                      start,
                      ts2ticks_ceil(&value->it_interval));
        }

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  unlock_and_einval:
    xnlock_put_irqrestore(&nklock, s);
  einval:
    thread_set_errno(EINVAL);
    return -1;
}

int timer_gettime(timer_t timerid, struct itimerspec *value)
{
    struct pse51_timer *timer;
    spl_t s;

    if ((unsigned) timerid >= PSE51_TIMER_MAX)
        goto einval;

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
        goto unlock_and_einval;

    pse51_timer_gettime_inner(timer, value);

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  unlock_and_einval:
    xnlock_put_irqrestore(&nklock, s);
  einval:
    thread_set_errno(EINVAL);
    return -1;
}

int timer_getoverrun(timer_t timerid)
{
    struct pse51_timer *timer;
    int overruns;
    spl_t s;

    if ((unsigned) timerid >= PSE51_TIMER_MAX)
        goto einval;

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
        goto unlock_and_einval;

    overruns = timer->last_overruns;

    xnlock_put_irqrestore(&nklock, s);

    return overruns;


  unlock_and_einval:
    xnlock_put_irqrestore(&nklock, s);
  einval:
    thread_set_errno(EINVAL);
    return -1;
}

void pse51_timer_init_thread(pthread_t new_thread)
{
    initq(&new_thread->timersq);
}
                
/* Called with nklock locked irq off. */
void pse51_timer_cleanup_thread(pthread_t zombie)
{
    xnholder_t *holder;
    while ((holder = getheadq(&zombie->timersq)) != NULL)
        {
        timer_t tm = link2tm(holder) - timer_pool;
#ifdef CONFIG_XENO_OPT_DEBUG
        xnprintf("Posix timer %d not destroyed, destroying now.\n", tm);
#endif /* CONFIG_XENO_OPT_DEBUG */
        timer_delete(tm);
        }
}


int pse51_timer_pkg_init(void)
{
    int n;

    initq(&timer_freeq);

    for (n = 0; n < PSE51_TIMER_MAX; n++)
	{
	inith(&timer_pool[n].link);
	appendq(&timer_freeq,&timer_pool[n].link);
	}

    return 0;
}

void pse51_timer_pkg_cleanup(void)
{
#ifdef CONFIG_XENO_OPT_DEBUG
    int n;

    for (n = 0; n < PSE51_TIMER_MAX; n++)
        if (timer_pool[n].owner)
            xnprintf("Posix timer %d was not deleted, deleting now.\n", n);
    /* Nothing to be done for deletion, since the pool is in static memory. */
#endif /* CONFIG_XENO_OPT_DEBUG */
}

EXPORT_SYMBOL(timer_create);
EXPORT_SYMBOL(timer_delete);
EXPORT_SYMBOL(timer_settime);
EXPORT_SYMBOL(timer_gettime);
EXPORT_SYMBOL(timer_getoverrun);
