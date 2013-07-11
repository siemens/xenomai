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
#include "thread.h"
#include "timer.h"
#include "internal.h"

static void *sigev_mem;

static LIST_HEAD(sigev_pool);

int cobalt_signal_send(struct cobalt_thread *thread, siginfo_t *si)
{				/* nklocked, IRQs off */
	struct cobalt_sigwait_context *swc;
	struct xnthread_wait_context *wc;
	struct cobalt_sigevent *se;
	struct list_head *sigq;
	int sig;

	sig = si->si_signo;
	XENO_BUGON(COBALT, sig < 1 || sig > _NSIG);
	sigq = thread->sigqueues + sig - 1;

	/*
	 * If the signal is already pending for the target thread, we
	 * check whether it originates from the same source, in which
	 * case we only bump the overrun count. For sources to match,
	 * we need to have si_code and the originator's id
	 * matching. Because si_pid and si_tid share the same memory
	 * offset in siginfo_t, testing either of them will do.
	 *
	 * If no source was matched, we queue another notification for
	 * real-time signals only.
	 */
	if (!list_empty(sigq)) {
		list_for_each_entry(se, sigq, next) {
			if (se->si.si_code == si->si_code &&
			    se->si.si_pid == si->si_pid) {
				if (se->si.si_overrun++ == COBALT_DELAYMAX)
					se->si.si_overrun = COBALT_DELAYMAX;
				return 0;
			}
		}
		if (sig < SIGRTMIN)
			return 0;
	}

	/* Can we deliver this signal immediately? */
	if (xnsynch_pended_p(&thread->sigwait)) {
		wc = xnthread_get_wait_context(&thread->threadbase);
		swc = container_of(wc, struct cobalt_sigwait_context, wc);
		if (sigismember(swc->set, sig)) {
			*swc->si = *si;
			xnsynch_wakeup_one_sleeper(&thread->sigwait);
			return 0;
		}
	}

	/*
	 * Ok, we definitely have to enqueue a signal notification. If
	 * running out of queue space, notify caller via -EAGAIN.
	 */
	if (list_empty(&sigev_pool)) {
		printk(XENO_ERR "sigev_pool empty, signal #%d to pid %d LOST!",
		       sig, xnthread_host_pid(&thread->threadbase));
		return -EAGAIN;
	}

	se = list_get_entry(&sigev_pool, struct cobalt_sigevent, next);
	se->si = *si;
	se->si.si_overrun = 0;
	sigaddset(&thread->sigpending, sig);
	list_add_tail(&se->next, sigq);

	return 0;
}

void cobalt_signal_flush(struct cobalt_thread *thread)
{
	struct cobalt_sigevent *se, *tmp;
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
		list_for_each_entry_safe(tmp, se, sigq, next) {
			list_del(&se->next);
			list_add(&se->next, &sigev_pool);
		}
	}

	sigemptyset(&thread->sigpending);
}

static int signal_wait(sigset_t *set, siginfo_t *si, xnticks_t timeout)
{
	struct cobalt_sigwait_context swc;
	struct cobalt_sigevent *se;
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
		se = list_get_entry(sigq, struct cobalt_sigevent, next);
		if (list_empty(sigq))
			sigdelset(&curr->sigpending, sig);
		*si = se->si;
		list_add(&se->next, &sigev_pool);
		ret = 0;
		goto done;
	}

wait:
	swc.set = set;
	swc.si = si;
	xnthread_prepare_wait(&swc.wc);
	ret = xnsynch_sleep_on(&curr->sigwait, timeout, XN_RELATIVE);
	xnthread_finish_wait(&swc.wc, NULL);
	if (ret)
		goto out;
done:
	/*
	 * We have to update the overrun count for each notified
	 * timer.
	 */
	if (si->si_code == SI_TIMER)
		cobalt_timer_notified(si->si_tid);
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_sigwait(const sigset_t __user *u_set, int __user *u_sig)
{
	siginfo_t si;
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

int cobalt_sigtimedwait(const sigset_t __user *u_set, siginfo_t __user *u_si,
			const struct timespec __user *u_timeout)
{
	struct timespec timeout;
	siginfo_t si;
	sigset_t set;
	int ret;

	if (__xn_safe_copy_from_user(&set, u_set, sizeof(set)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	if ((unsigned long)timeout.tv_nsec >= ONE_BILLION)
		return -EINVAL;

	ret = signal_wait(&set, &si, ts2ns(&timeout) + 1);
	if (ret)
		return ret == -ETIMEDOUT ? -EAGAIN : ret;

	if (__xn_safe_copy_to_user(u_si, &si, sizeof(*u_si)))
		return -EFAULT;

	return 0;
}

int cobalt_sigwaitinfo(const sigset_t __user *u_set, siginfo_t __user *u_si)
{
	siginfo_t si;
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

/*
 * How many signal notifications may be pending at any given time,
 * system-wide. Cobalt signals are always thread directed, and we
 * assume that in practice, each signal number is processed by a
 * dedicated thread. We provide for up to three real-time signal
 * events to pile up (coming from distinct sources), and a single
 * notification pending for other signals.
 */
#define __SEVPOOL_SIZE  (sizeof(struct cobalt_sigevent) *	\
			 (_NSIG + (SIGRTMAX - SIGRTMIN) * 2))

int cobalt_signal_pkg_init(void)
{
	struct cobalt_sigevent *se;

	sigev_mem = alloc_pages_exact(__SEVPOOL_SIZE, GFP_KERNEL);
	if (sigev_mem == NULL)
		return -ENOMEM;

	for (se = sigev_mem; (void *)se < sigev_mem + __SEVPOOL_SIZE; se++)
		list_add_tail(&se->next, &sigev_pool);

	return 0;
}

void cobalt_signal_pkg_cleanup(void)
{
	free_pages_exact(sigev_mem, __SEVPOOL_SIZE);
}
