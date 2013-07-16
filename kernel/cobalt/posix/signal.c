/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#include <cobalt/kernel/assert.h>
#include "internal.h"
#include "signal.h"
#include "thread.h"
#include "timer.h"
#include "clock.h"

static void *sigpending_mem;

static LIST_HEAD(sigpending_pool);

/*
 * How many signal notifications which may be pending at any given
 * time, except timers.  Cobalt signals are always thread directed,
 * and we assume that in practice, each signal number is processed by
 * a dedicated thread. We provide for up to three real-time signal
 * events to pile up, and a single notification pending for other
 * signals. Timers use a fast queuing logic maintaining a count of
 * overruns, and therefore do not consume any memory from this pool.
 */
#define __SIGPOOL_SIZE  (sizeof(struct cobalt_sigpending) *	\
			 (_NSIG + (SIGRTMAX - SIGRTMIN) * 2))

int cobalt_signal_send(struct cobalt_thread *thread,
		       struct cobalt_sigpending *sigp)
{				/* nklocked, IRQs off */
	struct cobalt_sigwait_context *swc;
	struct xnthread_wait_context *wc;
	struct list_head *sigq;
	int sig, ret;

	sig = sigp->si.si_signo;
	XENO_BUGON(COBALT, sig < 1 || sig > _NSIG);

	/* Can we deliver this signal immediately? */
	if (xnsynch_pended_p(&thread->sigwait)) {
		wc = xnthread_get_wait_context(&thread->threadbase);
		swc = container_of(wc, struct cobalt_sigwait_context, wc);
		if (sigismember(swc->set, sig)) {
			*swc->si = sigp->si;
			cobalt_call_extension(signal_deliver,
					      &thread->extref, ret, sigp);
			xnsynch_wakeup_one_sleeper(&thread->sigwait);
			return 0;
		}
	}

	/*
	 * Nope, attempt to queue it. We start by calling any Cobalt
	 * extension for queuing the signal first.
	 */
	if (cobalt_call_extension(signal_queue, &thread->extref, ret, sigp)) {
		if (ret < 0)
			return ret; /* Error. */
		if (ret > 0)
			return 0; /* Queuing done. */
	}

	sigq = thread->sigqueues + sig - 1;
	if (!list_empty(sigq)) {
		/* Queue non-rt signals only once. */
		if (sig < SIGRTMIN)
			return 0;
		/* Queue rt signal source only once (SI_TIMER). */
		if (!list_empty(&sigp->next))
			return 0;
	}

	sigaddset(&thread->sigpending, sig);
	list_add_tail(&sigp->next, sigq);

	return 0;
}

int cobalt_signal_send_pid(pid_t pid, struct cobalt_sigpending *sigp)
{				/* nklocked, IRQs off */
	struct cobalt_thread *thread;

	thread = cobalt_thread_find(pid);
	if (thread)
		return cobalt_signal_send(thread, sigp);

	return -ESRCH;
}

struct cobalt_sigpending *cobalt_signal_alloc(void)
{				/* nklocked, IRQs off */
	struct cobalt_sigpending *sigp;

	if (list_empty(&sigpending_pool))
		return NULL;

	sigp = list_get_entry(&sigpending_pool, struct cobalt_sigpending, next);
	INIT_LIST_HEAD(&sigp->next);

	return sigp;
}

void cobalt_signal_flush(struct cobalt_thread *thread)
{
	struct cobalt_sigpending *sigp, *tmp;
	struct list_head *sigq;
	int n;

	/*
	 * TCB is not accessible from userland anymore, no locking
	 * required.
	 */
	if (sigisemptyset(&thread->sigpending))
		return;

	for (n = 0; n < _NSIG; n++) {
		sigq = thread->sigqueues + n;
		if (list_empty(sigq))
			continue;
		/*
		 * sigpending blocks must be unlinked so that we
		 * detect this fact when deleting their respective
		 * owners.
		 */
		list_for_each_entry_safe(tmp, sigp, sigq, next)
			list_del_init(&sigp->next);
	}

	sigemptyset(&thread->sigpending);
}

static inline struct cobalt_sigpending *next_sigp(struct list_head *sigq)
{
	struct cobalt_sigpending *sigp;

	sigp = list_get_entry(sigq, struct cobalt_sigpending, next);

	if ((void *)sigp >= sigpending_mem &&
	    (void *)sigp < sigpending_mem + __SIGPOOL_SIZE)
		list_add_tail(&sigp->next, &sigpending_pool);
	else
		INIT_LIST_HEAD(&sigp->next);

	return sigp;
}

static int signal_wait(sigset_t *set, struct siginfo *si, xnticks_t timeout)
{
	struct cobalt_sigwait_context swc;
	struct cobalt_sigpending *sigp;
	struct cobalt_thread *curr;
	unsigned long *p, *t, m;
	struct list_head *sigq;
	int ret, sig, n;
	spl_t s;

	curr = cobalt_current_thread();
	XENO_BUGON(COBALT, curr == NULL);

	xnlock_get_irqsave(&nklock, s);

	if (sigisemptyset(&curr->sigpending))
		/* Most common/fast path. */
		goto wait;

	p = curr->sigpending.sig; /* pending */
	t = set->sig;		  /* tested */

	for (n = 0, sig = 0; n < _NSIG_WORDS; ++n) {
		m = *p++ & *t++;
		if (m == 0)
			continue;
		sig = ffz(~m) +  n *_NSIG_BPW + 1;
		break;
	}

	if (sig) {
		sigq = curr->sigqueues + sig - 1;
		XENO_BUGON(COBALT, list_empty(sigq));
		sigp = next_sigp(sigq);
		if (list_empty(sigq))
			sigdelset(&curr->sigpending, sig);
		*si = sigp->si;
		ret = 0;
		goto done;
	}

	if (timeout == XN_NONBLOCK) {
		ret = -EAGAIN;
		goto out;
	}
wait:
	swc.set = set;
	swc.si = si;
	xnthread_prepare_wait(&swc.wc);
	ret = xnsynch_sleep_on(&curr->sigwait, timeout, XN_RELATIVE);
	xnthread_finish_wait(&swc.wc, NULL);
	if (ret) {
		ret = ret & XNBREAK ? -EINTR : -EAGAIN;
		goto out;
	}
done:
	/* Compute the overrun count for timer-originated signals. */
	if (si->si_code == SI_TIMER)
		si->si_overrun = cobalt_timer_deliver(si->si_tid);

	/* Translate kernel codes for userland. */
	if (si->si_code & __SI_MASK)
		si->si_code |= __SI_MASK;
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_sigwait(const sigset_t __user *u_set, int __user *u_sig)
{
	struct siginfo si;
	sigset_t set;
	int ret;

	if (__xn_safe_copy_from_user(&set, u_set, sizeof(set)))
		return -EFAULT;

	ret = signal_wait(&set, &si, XN_INFINITE);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_sig, &si.si_signo, sizeof(*u_sig)))
		return -EFAULT;

	return 0;
}

int cobalt_sigtimedwait(const sigset_t __user *u_set, struct siginfo __user *u_si,
			const struct timespec __user *u_timeout)
{
	struct timespec timeout;
	struct siginfo si;
	xnticks_t ticks;
	sigset_t set;
	int ret;

	if (__xn_safe_copy_from_user(&set, u_set, sizeof(set)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	if ((unsigned long)timeout.tv_nsec >= ONE_BILLION)
		return -EINVAL;

	ticks = ts2ns(&timeout);
	if (ticks++ == 0)
		ticks = XN_NONBLOCK;

	ret = signal_wait(&set, &si, ticks);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_si, &si, sizeof(*u_si)))
		return -EFAULT;

	return 0;
}

int cobalt_sigwaitinfo(const sigset_t __user *u_set, struct siginfo __user *u_si)
{
	struct siginfo si;
	sigset_t set;
	int ret;

	if (__xn_safe_copy_from_user(&set, u_set, sizeof(set)))
		return -EFAULT;

	ret = signal_wait(&set, &si, XN_INFINITE);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_si, &si, sizeof(*u_si)))
		return -EFAULT;

	return 0;
}

int cobalt_sigpending(sigset_t __user *u_set)
{
	struct cobalt_thread *curr;

	curr = cobalt_current_thread();
	XENO_BUGON(COBALT, curr == NULL);
	
	if (__xn_safe_copy_to_user(u_set, &curr->sigpending, sizeof(*u_set)))
		return -EFAULT;

	return 0;
}

int cobalt_signal_pkg_init(void)
{
	struct cobalt_sigpending *sigp;

	sigpending_mem = alloc_pages_exact(__SIGPOOL_SIZE, GFP_KERNEL);
	if (sigpending_mem == NULL)
		return -ENOMEM;

	for (sigp = sigpending_mem;
	     (void *)sigp < sigpending_mem + __SIGPOOL_SIZE; sigp++)
		list_add_tail(&sigp->next, &sigpending_pool);

	return 0;
}

void cobalt_signal_pkg_cleanup(void)
{
	free_pages_exact(sigpending_mem, __SIGPOOL_SIZE);
}
