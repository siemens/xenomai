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

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
#include <xenomai/nucleus/shadow.h>
#endif /* __KERNEL__  && CONFIG_XENO_OPT_PERVASIVE*/
#include <asm/xenomai/system.h> /* For xnlock. */
#include <xenomai/posix/timer.h>        /* For pse51_timer_notified. */
#include <xenomai/posix/signal.h>

static void pse51_default_handler (int sig);

typedef void siginfo_handler_t(int, siginfo_t *, void *);

#define user2pse51_sigset(set) ((pse51_sigset_t *)(set))
#define PSE51_SIGQUEUE_MAX 64

static struct sigaction actions[SIGRTMAX];
static pse51_siginfo_t pse51_infos_pool[PSE51_SIGQUEUE_MAX];
#ifdef CONFIG_SMP
static xnlock_t pse51_infos_lock;
#endif
static xnpqueue_t pse51_infos_free_list;

static pse51_siginfo_t *pse51_new_siginfo (int sig, int code, union sigval value)
{
    xnpholder_t *holder;
    pse51_siginfo_t *si;
    spl_t s;

    xnlock_get_irqsave(&pse51_infos_lock, s);
    holder = getpq(&pse51_infos_free_list);
    xnlock_put_irqrestore(&pse51_infos_lock, s);

    if (!holder)
        return NULL;

    si = link2siginfo(holder);
    si->info.si_signo = sig;
    si->info.si_code = code;
    si->info.si_value = value;
    
    return si;
}

static void pse51_delete_siginfo (pse51_siginfo_t *si)
{
    spl_t s;

    initph(&si->link);
    si->info.si_signo = 0;      /* Used for debugging. */

    xnlock_get_irqsave(&pse51_infos_lock, s);
    insertpql(&pse51_infos_free_list, &si->link, 0);
    xnlock_put_irqrestore(&pse51_infos_lock, s);
}

static inline void emptyset (pse51_sigset_t *set) {

    *set = 0ULL;
}

static inline void fillset (pse51_sigset_t *set) {

    *set = ~0ULL;
}

static inline void addset (pse51_sigset_t *set, int sig) {

    *set |= ((pse51_sigset_t) 1 << (sig - 1));
}

static inline void delset (pse51_sigset_t *set, int sig) {

    *set &= ~((pse51_sigset_t) 1 << (sig - 1));
}

static inline int ismember (const pse51_sigset_t *set, int sig) {

    return (*set & ((pse51_sigset_t) 1 << (sig - 1))) != 0;
}

static inline int isemptyset (const pse51_sigset_t *set)
{
    return (*set) == 0ULL;
}

static inline void andset (pse51_sigset_t *set,
                           const pse51_sigset_t *left,
                           const pse51_sigset_t *right)
{
    *set = (*left) & (*right);
}

static inline void orset (pse51_sigset_t *set,
                          const pse51_sigset_t *left,
                          const pse51_sigset_t *right)
{
    *set = (*left) | (*right);
}

static inline void nandset (pse51_sigset_t *set,
                            const pse51_sigset_t *left,
                            const pse51_sigset_t *right)
{
    *set = (*left) & ~(*right);
}


int sigemptyset (sigset_t *user_set)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    
    emptyset(set);

    return 0;
}

int sigfillset (sigset_t *user_set)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    
    fillset(set);

    return 0;
}

int sigaddset (sigset_t *user_set, int sig)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    
    if ((unsigned ) (sig - 1) > SIGRTMAX - 1)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    addset(set, sig);

    return 0;
}

int sigdelset (sigset_t *user_set, int sig)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    
    if ((unsigned ) (sig - 1) > SIGRTMAX - 1)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    delset(set, sig);

    return 0;
}

int sigismember (const sigset_t *user_set, int sig)

{
    pse51_sigset_t *set=user2pse51_sigset(user_set);

    if ((unsigned ) (sig - 1) > SIGRTMAX - 1)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    return ismember(set,sig);
}

/* Must be called with nklock lock, irqs off, may reschedule. */
void pse51_sigqueue_inner (pthread_t thread, pse51_siginfo_t *si)
{
    unsigned prio;
    int signum;

    signum = si->info.si_signo;
    /* Since signals below SIGRTMIN are not real-time, they should be treated
       after real-time signals, hence their priority. */
    prio = signum < SIGRTMIN ? signum + SIGRTMAX : signum;

    initph(&si->link);
    
    if (ismember(&thread->sigmask, signum))
        {
        addset(&thread->blocked_received.mask, signum);
        insertpqf(&thread->blocked_received.list, &si->link, prio);
        }    
    else
        {
        addset(&thread->pending.mask, signum);
        insertpqf(&thread->pending.list, &si->link, prio);
        thread->threadbase.signals = 1;
        }

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    /* POSIX shadow signals ping: in order to send a signal to a user-space
       POSIX thread, we get it back to primary mode. */
    if (testbits(thread->threadbase.status, XNRELAX) &&
         xnthread_user_task(&thread->threadbase))
        xnshadow_suspend(&thread->threadbase);
#endif /* __KERNEL__  && CONFIG_XENO_OPT_PERVASIVE*/

    if (thread == pse51_current_thread()
        || xnpod_unblock_thread(&thread->threadbase))
        xnpod_schedule();
}

void pse51_sigunqueue (pthread_t thread, pse51_siginfo_t *si)
{
    pse51_sigqueue_t *queue;
    xnpholder_t *next;

    if (ismember(&thread->sigmask, si->info.si_signo))
        queue = &thread->blocked_received;
    else
        queue = &thread->pending;

    /* If si is the only signal queued with its signal number, clear the
       mask. We do not have "prevpq", we hence use findpq, even though this is
       much less efficient. */
    next = nextpq(&queue->list, &si->link);
    if ((!next || next->prio != si->link.prio)
       && findpqh(&queue->list, si->link.prio) == &si->link)
        delset(&queue->mask, si->info.si_signo);

    removepq(&queue->list, &si->link);
}


/* Unqueue any siginfo of "queue" whose signal number is member of "set",
   starting with "start". If "start" is NULL, start from the list head. */
static pse51_siginfo_t *pse51_getsigq (pse51_sigqueue_t *queue,
                                       pse51_sigset_t *set,
                                       pse51_siginfo_t **start)
{
    xnpholder_t *holder, *next;
    pse51_siginfo_t *si;

    next = (start && *start) ? &(*start)->link : getheadpq(&queue->list);

    while ((holder = next))
        {
        next = nextpq(&queue->list, holder);
        si = link2siginfo(holder);
        
        if (ismember(set, si->info.si_signo))
            goto found;
        }

    if (start)
        *start = NULL;

    return NULL;

  found:
    removepq(&queue->list, holder);
    if (!next || next->prio != holder->prio)
        delset(&queue->mask, si->info.si_signo);

    if (start)
        *start = next ? link2siginfo(next) : NULL;
    
    return si;
}

int sigaction (int sig, const struct sigaction *action, struct sigaction *old)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ((unsigned) (sig - 1) > SIGRTMAX - 1)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    if (action && testbits(action->sa_flags, ~SIGACTION_FLAGS))
	{
        thread_set_errno(ENOTSUP);
        return -1;
	}

    xnlock_get_irqsave(&nklock, s);

    if (old)
        *old = actions[sig - 1];

    if (action)
	{
        struct sigaction *dest_action = &actions[sig - 1];
            
        *dest_action = *action;

        if (!(testbits(action->sa_flags, SA_NOMASK)))
            addset(user2pse51_sigset(&dest_action->sa_mask), sig);
	}

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigqueue (pthread_t thread, int sig, union sigval value)
{
    pse51_siginfo_t *si = NULL; /* Avoid spurious warning. */
    spl_t s;

    if ((unsigned) (sig - 1) > SIGRTMAX - 1)
        return EINVAL;

    if (sig)
        {
        si = pse51_new_siginfo(sig, SI_QUEUE, value);

        if (!si)
            return EAGAIN;
        }

    xnlock_get_irqsave(&nklock, s);    

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC,struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (sig)
        pse51_sigqueue_inner(thread, si);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_kill (pthread_t thread, int sig)
{
    pse51_siginfo_t *si = NULL;
    spl_t s;

    if ((unsigned) (sig - 1) > SIGRTMAX - 1)
        return EINVAL;

    if (sig)
        {
        si = pse51_new_siginfo(sig, SI_USER, (union sigval) 0);

        if (!si)
            return EAGAIN;
        }

    xnlock_get_irqsave(&nklock, s);    

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC,struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (sig)
        pse51_sigqueue_inner(thread, si);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigpending (sigset_t *user_set)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    /* Lock nklock, in order to prevent pthread_kill from modifying
     * blocked_received while we are reading */
    xnlock_get_irqsave(&nklock, s);  

    *set = pse51_current_thread()->blocked_received.mask;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_sigmask (int how, const sigset_t *user_set, sigset_t *user_oset)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    pse51_sigset_t *oset = user2pse51_sigset(user_oset);
    pse51_sigset_t unblocked;
    pthread_t cur;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    emptyset(&unblocked);

    xnlock_get_irqsave(&nklock, s);

    cur = pse51_current_thread();

    if (oset)
        *oset = cur->sigmask;

    if (!set)
	goto unlock_and_exit;

    if (xnthread_signaled_p(&cur->threadbase))
	/* Call xnpod_schedule to deliver any soon-to-be-blocked pending
           signal, after this call, no signal is pending. */
	xnpod_schedule();

    switch (how)
	{

	case SIG_BLOCK:

	    orset(&cur->sigmask, &cur->sigmask, set);
	    break;

	case SIG_UNBLOCK:
	    /* Mark as pending any signal which was received while
	       blocked and is going to be unblocked. */
            andset(&unblocked, set, &cur->blocked_received.mask);
            nandset(&cur->sigmask, &cur->pending.mask, &unblocked);
	    break;

	case SIG_SETMASK:

            nandset(&unblocked, &cur->blocked_received.mask, set);
            cur->sigmask = *set;
	    break;

	default:

	    xnlock_put_irqrestore(&nklock, s);
	    return EINVAL;
	}
        
    /* Handle any unblocked signal. */
    if (!isemptyset(&unblocked))
        {
        pse51_siginfo_t *si, *next = NULL;

        cur->threadbase.signals = 0;

        while ((si = pse51_getsigq(&cur->blocked_received, &unblocked, &next)))
            {
            int sig = si->info.si_signo;
            unsigned prio;

            prio = sig < SIGRTMIN ? sig + SIGRTMAX : sig;
            addset(&cur->pending.mask, si->info.si_signo);
            insertpqf(&cur->pending.list, &si->link, prio);
            cur->threadbase.signals = 1;

            if (!next)
                break;
            }

        /* Let pse51_dispatch_signals do the job. */
        if (cur->threadbase.signals)
            xnpod_schedule();
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

static int pse51_sigtimedwait_inner (const sigset_t *user_set,
                                     siginfo_t *si,
                                     xnticks_t to)
{
    pse51_sigset_t non_blocked, *set = user2pse51_sigset(user_set);
    pse51_siginfo_t *received;
    pthread_t thread;
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    thread = pse51_current_thread();

    /* All signals in "set" must be blocked in order for sigwait to
       work reliably. */
    nandset(&non_blocked, set, &thread->sigmask);
    if (!isemptyset(&non_blocked))
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    received = pse51_getsigq(&thread->blocked_received, set, NULL);
    
    if (!received)
	{
        err = clock_adjust_timeout(&to, CLOCK_MONOTONIC);

        if (err)
            {
            if (err == ETIMEDOUT)
                err = EAGAIN;
            
            goto unlock_and_ret;
            }
        
        xnpod_suspend_thread(&thread->threadbase, XNDELAY, to, NULL);

        thread_cancellation_point(thread);

        if (xnthread_test_flags(&thread->threadbase, XNBREAK))
            {
            if (!(received = pse51_getsigq(&thread->blocked_received,
                                           set,
                                           NULL)))
                err = EINTR;
            }
        else if (xnthread_test_flags(&thread->threadbase, XNTIMEO))
            err = EAGAIN;
	}

    if (!err)
        {
        *si = received->info;
        if (si->si_code == SI_QUEUE || si->si_code == SI_USER)
            pse51_delete_siginfo(received);
        else if (si->si_code == SI_TIMER)
            pse51_timer_notified(received);
        }

  unlock_and_ret:
    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int sigwait (const sigset_t *user_set, int *sig)
{
    siginfo_t info;
    int err;

    do
        {
        err = pse51_sigtimedwait_inner(user_set, &info, XN_INFINITE);
        }
    while (err == EINTR);

    if (!err)
        *sig = info.si_signo;

    return err;
}

int sigwaitinfo (const sigset_t *__restrict__ user_set,
                 siginfo_t *__restrict__ info)
{
    siginfo_t loc_info;
    int err;

    if (!info)
        info = &loc_info;

    do
        {
        err = pse51_sigtimedwait_inner(user_set, info, XN_INFINITE);
        }
    while (err == EINTR);

    /* Sigwaitinfo does not have the same behaviour as sigwait, errors are
       returned through errno. */
    if (err)
        {
        thread_set_errno(err);
        return -1;
        }
    
    return 0;
}

int sigtimedwait (const sigset_t *__restrict__ user_set,
                  siginfo_t *__restrict__ info,
                  const struct timespec *__restrict__ timeout)
{
    xnticks_t abs_timeout;
    int err;

    if (timeout)
        {
        if ((unsigned) timeout->tv_nsec > ONE_BILLION)
            {
            err = EINVAL;
            goto out;
            }

        abs_timeout = clock_get_ticks(CLOCK_MONOTONIC)+ts2ticks_ceil(timeout)+1;
        }
    else
        abs_timeout = XN_INFINITE;

    do 
        {
        err = pse51_sigtimedwait_inner(user_set, info, abs_timeout);
        }
    while (err == EINTR);

  out:
    if (err)
        {
        thread_set_errno(err);
        return -1;
        }

    return 0;
}

static void pse51_dispatch_signals (xnsigmask_t sigs)

{
    pse51_siginfo_t *si, *next = NULL;
    pse51_sigset_t saved_mask;
    pthread_t thread;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    thread = pse51_current_thread();
    
    saved_mask = thread->sigmask;

    while ((si = pse51_getsigq(&thread->pending, &thread->pending.mask, &next)))
        {
        struct sigaction *action = &actions[si->info.si_signo - 1];
        siginfo_t info = si->info;

        if (si->info.si_code == SI_TIMER)
            pse51_timer_notified(si);

        if (si->info.si_code == SI_QUEUE || si->info.si_code == SI_USER)
            pse51_delete_siginfo(si);

        if (action->sa_handler != SIG_IGN)
            {
            siginfo_handler_t *info_handler =
                (siginfo_handler_t *) action->sa_sigaction;
            sighandler_t handler = action->sa_handler;

            if (handler == SIG_DFL)
                handler = pse51_default_handler;

            thread->sigmask = *user2pse51_sigset(&action->sa_mask);

            if (testbits(action->sa_flags, SA_ONESHOT))
                action->sa_handler = SIG_DFL;

            if (!testbits(action->sa_flags, SA_SIGINFO) || handler == SIG_DFL)
                handler(info.si_signo);
            else
                info_handler(info.si_signo, &info, NULL);
            }

        if (!next)
            break;
	}

    thread->sigmask = saved_mask;
    thread->threadbase.signals = 0;

    xnlock_put_irqrestore(&nklock, s);
}

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
/* The way signals are handled for shadows has not much in common with the way
   they are handled for kernel-space threads. We hence use a different
   function. */
static void pse51_dispatch_shadow_signals (xnsigmask_t sigs)
{
    pse51_siginfo_t *si;
    pthread_t thread;
    spl_t ignored;

    thread = pse51_current_thread();
    
    __setbits(thread->threadbase.status, XNASDI);

    /* POSIX shadow signals pong: to get the signals dispatch function executed,
       we migrated the shadow to primary mode, we are going to migrate back to
       secondary mode in order to get the signals delivered by Linux.
       Release the big lock, we do not want to hold it during migration.
    */
    xnlock_clear_irqon(&nklock);
    xnshadow_relax(1);

    xnlock_get_irqsave(&nklock, ignored);
    
    thread->threadbase.signals = 0;

    while ((si = pse51_getsigq(&thread->pending,
                               &thread->pending.mask,
                               NULL)))
        {
        siginfo_t info = si->info;

        if (si->info.si_code == SI_TIMER)
            pse51_timer_notified(si);
        
        if (si->info.si_code == SI_QUEUE || si->info.si_code == SI_USER)
            pse51_delete_siginfo(si);

        /* Release the big lock, before calling a function which may
           reschedule. */
        xnlock_clear_irqon(&nklock);

        send_sig_info(info.si_signo, &info, current);

        xnlock_get_irqsave(&nklock, ignored);

        thread->threadbase.signals = 0;
        }

    __clrbits(thread->threadbase.status, XNASDI);

    return;
}
#endif /* __KERNEL__  && CONFIG_XENO_OPT_PERVASIVE*/
    
void pse51_signal_init_thread (pthread_t newthread, const pthread_t parent)

{
    emptyset(&newthread->blocked_received.mask);
    initpq(&newthread->blocked_received.list, xnqueue_up, SIGRTMAX + SIGRTMIN);
    emptyset(&newthread->pending.mask);
    initpq(&newthread->pending.list, xnqueue_up, SIGRTMAX + SIGRTMIN);

    /* parent may be NULL if pthread_create is not called from a pse51 thread. */
    if (parent)
        newthread->sigmask = parent->sigmask;
    else
        emptyset(&newthread->sigmask);

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    if (testbits(newthread->threadbase.status, XNSHADOW))
        newthread->threadbase.asr = &pse51_dispatch_shadow_signals;
    else
#endif /* __KERNEL__  && CONFIG_XENO_OPT_PERVASIVE*/
        newthread->threadbase.asr = &pse51_dispatch_signals;

    newthread->threadbase.asrmode = 0;
    newthread->threadbase.asrimask = 0;
}

/* Unqueue, and free any pending siginfo structure. Assume we are called nklock
   locked, IRQ off. */
void pse51_signal_cleanup_thread (pthread_t thread)
{
    pse51_sigqueue_t *queue = &thread->pending;
    pse51_siginfo_t *si;

    while (queue)
        {
        while ((si = pse51_getsigq(queue, &queue->mask, NULL)))
            {
            if (si->info.si_code == SI_TIMER)
                pse51_timer_notified(si);
            
            if (si->info.si_code == SI_QUEUE || si->info.si_code == SI_USER)
                pse51_delete_siginfo(si);
            }

        queue = (queue == &thread->pending ? &thread->blocked_received : NULL);
        }
}

void pse51_signal_pkg_init (void)

{
    int i;

    /* Fill the pool. */
    initpq(&pse51_infos_free_list, xnqueue_up, 1);
    for (i = 0; i < PSE51_SIGQUEUE_MAX; i++)
        pse51_delete_siginfo(&pse51_infos_pool[i]);

    for (i = 1; i <= SIGRTMAX; i++)
	{
        actions[i-1].sa_handler = SIG_DFL;
        emptyset(user2pse51_sigset(&actions[i-1].sa_mask));
        actions[i-1].sa_flags = 0;
	}
}

void pse51_signal_pkg_cleanup (void)

{
#ifdef CONFIG_XENO_OPT_DEBUG
    int i;

    for (i = 0; i < PSE51_SIGQUEUE_MAX; i++)
        if (pse51_infos_pool[i].info.si_signo)
            xnprintf("Posix siginfo structure %p was not freed, freeing now.\n",
                     &pse51_infos_pool[i].info);
#endif /* CONFIG_XENO_OPT_DEBUG */
}

static void pse51_default_handler (int sig)

{
    pthread_t cur = pse51_current_thread();
    
    xnpod_fatal("Thread %s received unhandled signal %d.\n",
                thread_name(cur), sig);
}

EXPORT_SYMBOL(sigemptyset);
EXPORT_SYMBOL(sigfillset);
EXPORT_SYMBOL(sigaddset);
EXPORT_SYMBOL(sigdelset);
EXPORT_SYMBOL(sigismember);
EXPORT_SYMBOL(pthread_kill);
EXPORT_SYMBOL(pthread_sigmask);
EXPORT_SYMBOL(pse51_sigaction);
EXPORT_SYMBOL(pse51_sigqueue);

EXPORT_SYMBOL(sigpending);
EXPORT_SYMBOL(sigwait);
EXPORT_SYMBOL(sigwaitinfo);
EXPORT_SYMBOL(sigtimedwait);
