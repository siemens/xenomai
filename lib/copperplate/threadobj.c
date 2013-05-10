/*
 * Copyright (C) 2008-2011 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 * Thread object abstraction.
 */

#include <signal.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sched.h>
#include "copperplate/lock.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/cluster.h"
#include "copperplate/clockobj.h"
#include "copperplate/eventobj.h"
#include "copperplate/heapobj.h"
#include "internal.h"

union copperplate_wait_union {
	struct syncluster_wait_struct syncluster_wait;
	struct eventobj_wait_struct eventobj_wait;
};

static void finalize_thread(void *p);

/*
 * NOTE on cancellation handling: Most traditional RTOSes guarantee
 * that the task/thread delete operation is strictly synchronous,
 * i.e. the deletion service returns to the caller only __after__ the
 * deleted thread entered an innocuous state, i.e. dormant/dead.
 *
 * For this reason, we always wait for the cancelled threads
 * internally (see threadobj_cancel()), which might lead to a priority
 * inversion. This is the price for guaranteeing that
 * threadobj_cancel() returns only after the cancelled thread
 * finalizer has run.
 */

int threadobj_high_prio;

int threadobj_irq_prio;

#ifdef HAVE_TLS

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct threadobj *__threadobj_current;

#endif

/*
 * We need the thread object key regardless of whether TLS is
 * available to us, to run the thread finalizer routine.
 */
pthread_key_t threadobj_tskey;

static inline void threadobj_init_key(void)
{
	if (pthread_key_create(&threadobj_tskey, finalize_thread))
		panic("failed to allocate TSD key");
}

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

static inline void pkg_init_corespec(void)
{
}

static inline void threadobj_init_corespec(struct threadobj *thobj)
{
}

static inline int threadobj_setup_corespec(struct threadobj *thobj)
{
	pthread_set_name_np(pthread_self(), thobj->name);
	thobj->core.handle = xeno_get_current();
	thobj->core.u_window = xeno_get_current_window();

	return 0;
}

static inline void threadobj_cleanup_corespec(struct threadobj *thobj)
{
}

static inline void threadobj_run_corespec(struct threadobj *thobj)
{
	__cobalt_thread_harden();
}

static inline void threadobj_cancel_corespec(struct threadobj *thobj) /* thobj->lock held */
{
	/*
	 * Send a SIGDEMT signal to demote the target thread, to make
	 * sure pthread_cancel() will be effective asap.
	 *
	 * In effect, the thread is kicked out of any blocking
	 * syscall, a relax is forced on it (via a mayday trap if
	 * required), and it is then required to leave the real-time
	 * scheduling class.
	 *
	 * - this makes sure the thread returns with EINTR from the
	 * syscall then hits a cancellation point asap.
	 *
	 * - this ensures that the thread can receive the cancellation
	 * signal in case asynchronous cancellation is enabled and get
	 * kicked out from syscall-less code in primary mode
	 * (e.g. busy loops).
	 *
	 * - this makes sure the thread won't preempt the caller
	 * indefinitely when resuming due to priority enforcement
	 * (i.e. when the target thread has higher Xenomai priority
	 * than the caller of threadobj_cancel()), but will receive
	 * the following cancellation request asap.
	 */
	__RT(pthread_kill(thobj->tid, SIGDEMT));
}

int threadobj_suspend(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret;

	__threadobj_check_locked(thobj);

	thobj->status |= __THREAD_S_SUSPENDED;
	threadobj_unlock(thobj);
	ret = __RT(pthread_kill(tid, SIGSUSP));
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_resume(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret;

	__threadobj_check_locked(thobj);

	if (thobj == threadobj_current())
		return 0;

	thobj->status &= ~__THREAD_S_SUSPENDED;
	threadobj_unlock(thobj);
	ret = __RT(pthread_kill(tid, SIGRESM));
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_lock_sched(void) /* current->lock held */
{
	struct threadobj *current = threadobj_current();

	__threadobj_check_locked(current);

	if (current->schedlock_depth++ > 0)
		return 0;

	current->status |= __THREAD_S_NOPREEMPT;
	/*
	 * In essence, we can't be scheduled out as a result of
	 * locking the scheduler, so no need to drop the thread lock
	 * across this call.
	 */
	return __bt(-pthread_set_mode_np(0, PTHREAD_LOCK_SCHED, NULL));
}

int threadobj_unlock_sched(void) /* current->lock held */
{
	struct threadobj *current = threadobj_current();

	__threadobj_check_locked(current);

	/*
	 * Higher layers may not know about the current locking level
	 * and fully rely on us to track it, so we gracefully handle
	 * unbalanced calls here, and let them decide of the outcome
	 * in case of error.
	 */
	if (current->schedlock_depth == 0)
		return __bt(-EINVAL);

	if (--current->schedlock_depth > 0)
		return 0;

	current->status &= ~__THREAD_S_NOPREEMPT;

	return __bt(-pthread_set_mode_np(PTHREAD_LOCK_SCHED, 0, NULL));
}

int threadobj_set_priority(struct threadobj *thobj, int prio) /* thobj->lock held, dropped */
{
	struct sched_param_ex xparam;
	pthread_t tid = thobj->tid;
	int policy;

	__threadobj_check_locked(thobj);

	policy = SCHED_RT;
	if (prio == 0) {
		thobj->status &= ~__THREAD_S_RR;
		policy = SCHED_OTHER;
	} else if (thobj->status & __THREAD_S_RR) {
		xparam.sched_rr_quantum = thobj->tslice;
		policy = SCHED_RR;
	}

	thobj->priority = prio;
	thobj->policy = policy;
	threadobj_unlock(thobj);
	/*
	 * XXX: as a side effect, resetting SCHED_RR will refill the
	 * time credit for the target thread with the last quantum
	 * set.
	 */
	xparam.sched_priority = prio;

	return pthread_setschedparam_ex(tid, policy, &xparam);
}

int threadobj_set_mode(int clrmask, int setmask, int *mode_r) /* current->lock held */
{
	struct threadobj *current = threadobj_current();
	int ret, __clrmask = 0, __setmask = 0;

	__threadobj_check_locked(current);

	if (setmask & __THREAD_M_LOCK)
		__setmask |= PTHREAD_LOCK_SCHED;
	else if (clrmask & __THREAD_M_LOCK)
		__clrmask |= PTHREAD_LOCK_SCHED;

	if (setmask & __THREAD_M_WARNSW)
		__setmask |= PTHREAD_WARNSW;
	else if (clrmask & __THREAD_M_WARNSW)
		__clrmask |= PTHREAD_WARNSW;

	if (setmask & __THREAD_M_CONFORMING)
		__setmask |= PTHREAD_CONFORMING;
	else if (clrmask & __THREAD_M_CONFORMING)
		__clrmask |= PTHREAD_CONFORMING;

	threadobj_unlock(current);
	ret = pthread_set_mode_np(__clrmask, __setmask, mode_r);
	threadobj_lock(current);

	return ret;
}

static int set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	struct sched_param_ex xparam;
	pthread_t tid = thobj->tid;
	int ret, policy;

	if (quantum && (quantum->tv_sec || quantum->tv_nsec)) {
		policy = SCHED_RR;
		xparam.sched_rr_quantum = *quantum;
		thobj->status |= __THREAD_S_RR;
		thobj->tslice = *quantum;
		xparam.sched_priority = thobj->priority ?: 1;
	} else {
		policy = thobj->policy;
		thobj->status &= ~__THREAD_S_RR;
		xparam.sched_rr_quantum.tv_sec = 0;
		xparam.sched_rr_quantum.tv_nsec = 0;
		xparam.sched_priority = thobj->priority;
	}

	threadobj_unlock(thobj);
	ret = pthread_setschedparam_ex(tid, policy, &xparam);
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_set_periodic(struct threadobj *thobj,
			   struct timespec *idate, struct timespec *period)
{
	return -pthread_make_periodic_np(thobj->tid,
					 CLOCK_COPPERPLATE, idate, period);
}

int threadobj_wait_period(unsigned long *overruns_r)
{
	return -pthread_wait_np(overruns_r);
}

int threadobj_stat(struct threadobj *thobj, struct threadobj_stat *p) /* thobj->lock held */
{
	struct cobalt_threadstat stat;
	int ret;

	__threadobj_check_locked(thobj);

	ret = __cobalt_thread_stat(thobj->pid, &stat);
	if (ret)
		return __bt(ret);

	p->cpu = stat.cpu;
	p->status = stat.status;
	p->xtime = stat.xtime;
	p->msw = stat.msw;
	p->csw = stat.csw;
	p->xsc = stat.xsc;
	p->pf = stat.pf;
	p->timeout = stat.timeout;

	return 0;
}

#else /* CONFIG_XENO_MERCURY */

#include <sys/prctl.h>
#include "copperplate/notifier.h"

static int threadobj_lock_prio;

static void unblock_sighandler(int sig)
{
	/*
	 * nop -- we just want the receiving thread to unblock with
	 * EINTR if applicable.
	 */
}

static void roundrobin_handler(int sig)
{
	struct threadobj *current = threadobj_current();

	/*
	 * We do manual round-robin over SCHED_FIFO(RT) to allow for
	 * multiple arbitrary time slices (i.e. vs the kernel
	 * pre-defined and fixed one).
	 */
	if (current && (current->status & __THREAD_S_RR) != 0)
		sched_yield();
}

static inline void pkg_init_corespec(void)
{
	struct sigaction sa;

	/*
	 * We don't have builtin scheduler-lock feature over Mercury,
	 * so we emulate it by reserving the highest priority level of
	 * the SCHED_RT class to disable involuntary preemption.
	 */
	threadobj_lock_prio = threadobj_high_prio;
	threadobj_high_prio = threadobj_lock_prio - 1;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = unblock_sighandler;
	sigaction(SIGRELS, &sa, NULL);
	sa.sa_handler = roundrobin_handler;
	sigaction(SIGVTALRM, &sa, NULL);

	notifier_pkg_init();
}

static void notifier_callback(const struct notifier *nf)
{
	struct threadobj *current;

	current = container_of(nf, struct threadobj, core.notifier);
	assert(current == threadobj_current());

	/*
	 * In the Mercury case, we mark the thread as suspended only
	 * when the notifier handler is entered, not from
	 * threadobj_suspend().
	 */
	threadobj_lock(current);
	current->status |= __THREAD_S_SUSPENDED;
	threadobj_unlock(current);
	notifier_wait(nf); /* Wait for threadobj_resume(). */
	threadobj_lock(current);
	current->status &= ~__THREAD_S_SUSPENDED;
	threadobj_unlock(current);
}

static inline void threadobj_init_corespec(struct threadobj *thobj)
{
	pthread_condattr_t cattr;
	/*
	 * Over Mercury, we need an additional per-thread condvar to
	 * implement the complex monitor for the syncobj abstraction.
	 */
	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE);
	pthread_cond_init(&thobj->core.grant_sync, &cattr);
	pthread_condattr_destroy(&cattr);
	thobj->core.rr_timer = NULL;
}

static inline int threadobj_setup_corespec(struct threadobj *thobj)
{
	struct sigevent sev;
	int ret;

	prctl(PR_SET_NAME, (unsigned long)thobj->name, 0, 0, 0);
	notifier_init(&thobj->core.notifier, notifier_callback, 1);
	thobj->core.period = 0;

	/*
	 * Create the per-thread round-robin timer.
	 *
	 * XXX: It is a bit overkill doing this here instead of on
	 * demand, but we must get the internal ID from the running
	 * thread, and unlike with set_rr(), threadobj_current() ==
	 * thobj is guaranteed in threadobj_setup_corespec().
	 */
	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL|SIGEV_THREAD_ID;
	sev.sigev_signo = SIGVTALRM;
	sev.sigev_notify_thread_id = copperplate_get_tid();

	ret = timer_create(CLOCK_THREAD_CPUTIME_ID, &sev,
			   &thobj->core.rr_timer);
	if (ret)
		return __bt(-errno);

	return 0;
}

static inline void threadobj_cleanup_corespec(struct threadobj *thobj)
{
	notifier_destroy(&thobj->core.notifier);
	pthread_cond_destroy(&thobj->core.grant_sync);
	if (thobj->core.rr_timer)
		timer_delete(thobj->core.rr_timer);
}

static inline void threadobj_run_corespec(struct threadobj *thobj)
{
}

static inline void threadobj_cancel_corespec(struct threadobj *thobj)
{
}

int threadobj_suspend(struct threadobj *thobj) /* thobj->lock held */
{
	struct notifier *nf = &thobj->core.notifier;
	int ret;

	threadobj_unlock(thobj); /* FIXME: racy */
	ret = notifier_signal(nf);
	threadobj_lock(thobj);

	return __bt(ret);
}

int threadobj_resume(struct threadobj *thobj) /* thobj->lock held */
{
	__threadobj_check_locked(thobj);

	if (thobj == threadobj_current())
		return 0;

	return __bt(notifier_release(&thobj->core.notifier));
}

int threadobj_lock_sched(void) /* current->lock held */
{
	struct threadobj *current = threadobj_current();
	pthread_t tid = current->tid;
	struct sched_param param;

	__threadobj_check_locked(current);

	if (current->schedlock_depth++ > 0)
		return 0;

	current->core.prio_unlocked = current->priority;
	current->core.policy_unlocked = current->policy;
	current->status |= __THREAD_S_NOPREEMPT;
	current->priority = threadobj_lock_prio;
	current->policy = SCHED_RT;
	param.sched_priority = threadobj_lock_prio;

	return __bt(-pthread_setschedparam(tid, SCHED_RT, &param));
}

int threadobj_unlock_sched(void) /* current->lock held */
{
	struct threadobj *current = threadobj_current();
	pthread_t tid = current->tid;
	struct sched_param param;
	int policy, ret;

	__threadobj_check_locked(current);

	if (current->schedlock_depth == 0)
		return __bt(-EINVAL);

	if (--current->schedlock_depth > 0)
		return 0;

	current->status &= ~__THREAD_S_NOPREEMPT;
	current->priority = current->core.prio_unlocked;
	param.sched_priority = current->core.prio_unlocked;
	policy = current->core.policy_unlocked;
	threadobj_unlock(current);
	ret = pthread_setschedparam(tid, policy, &param);
	threadobj_lock(current);

	return __bt(-ret);
}

int threadobj_set_priority(struct threadobj *thobj, int prio) /* thobj->lock held, dropped */
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int policy;

	__threadobj_check_locked(thobj);

	/*
	 * We don't actually change the scheduling priority in case
	 * the target thread holds the scheduler lock, but only record
	 * the level to set when unlocking.
	 */
	if (thobj->status & __THREAD_S_NOPREEMPT) {
		thobj->core.prio_unlocked = prio;
		thobj->core.policy_unlocked = prio ? SCHED_RT : SCHED_OTHER;
		return 0;
	}

	thobj->priority = prio;
	policy = SCHED_RT;
	if (prio == 0) {
		thobj->status &= ~__THREAD_S_RR;
		policy = SCHED_OTHER;
	}

	thobj->priority = prio;
	thobj->policy = policy;
	threadobj_unlock(thobj);
	/*
	 * Since we released the thread container lock, we now rely on
	 * the pthread interface to recheck the tid for existence.
	 */
	param.sched_priority = prio;

	return pthread_setschedparam(tid, policy, &param);
}

int threadobj_set_mode(int clrmask, int setmask, int *mode_r) /* current->lock held */
{
	struct threadobj *current = threadobj_current();
	int ret = 0, old = 0;

	__threadobj_check_locked(current);

	if (current->status & __THREAD_S_NOPREEMPT)
		old |= __THREAD_M_LOCK;

	if (setmask & __THREAD_M_LOCK)
		ret = __bt(threadobj_lock_sched_once());
	else if (clrmask & __THREAD_M_LOCK)
		threadobj_unlock_sched();

	if (mode_r)
		*mode_r = old;

	return ret;
}

static inline int set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	struct itimerspec value;
	int policy, ret;

	if (quantum && (quantum->tv_sec || quantum->tv_nsec)) {
		value.it_interval = *quantum;
		value.it_value = *quantum;
		thobj->tslice = *quantum;

		if (thobj->status & __THREAD_S_RR) {
			/* Changing quantum of ongoing RR. */
			ret = timer_settime(thobj->core.rr_timer, 0, &value, NULL);
			return ret ? __bt(-errno) : 0;
		}

		thobj->status |= __THREAD_S_RR;
		/*
		 * Switch to SCHED_FIFO policy, assign default prio=1
		 * if coming from SCHED_OTHER. We use a per-thread
		 * timer to implement manual round-robin.
		 */
		policy = SCHED_FIFO;
		param.sched_priority = thobj->priority ?: 1;
		ret = timer_settime(thobj->core.rr_timer, 0, &value, NULL);
		if (ret)
			return __bt(-errno);
	} else {
		if ((thobj->status & __THREAD_S_RR) == 0)
			return 0;
		thobj->status &= ~__THREAD_S_RR;
		/*
		 * Disarm timer and reset scheduling parameters to
		 * former policy.
		 */
		value.it_value.tv_sec = 0;
		value.it_value.tv_nsec = 0;
		value.it_interval = value.it_value;
		timer_settime(thobj->core.rr_timer, 0, &value, NULL);
		param.sched_priority = thobj->priority;
		policy = thobj->policy;
	}

	threadobj_unlock(thobj);
	ret = pthread_setschedparam(tid, policy, &param);
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_set_periodic(struct threadobj *thobj,
			   struct timespec *idate, struct timespec *period)
{
	struct timespec now, wakeup;

	clock_gettime(CLOCK_COPPERPLATE, &now);

	if (idate->tv_sec || idate->tv_nsec) {
		if (timespec_before(idate, &now))
			return -ETIMEDOUT;
		wakeup = *idate;
	} else
		wakeup = now;

	timespec_add(&thobj->core.wakeup, &wakeup, period);
	thobj->core.period = timespec_scalar(period);

	return 0;
}

int threadobj_wait_period(unsigned long *overruns_r)
{
	struct threadobj *current = threadobj_current();
	struct timespec now, delta, wakeup;
	unsigned long overruns = 0;
	ticks_t d, period;
	int ret;

	period = current->core.period;
	if (period == 0)
		return -EWOULDBLOCK;

	wakeup = current->core.wakeup;
	ret = threadobj_sleep(&wakeup);
	if (ret)
		return ret;

	/* Check whether we had an overrun. */

	clock_gettime(CLOCK_COPPERPLATE, &now);

	timespec_sub(&delta, &now, &wakeup);
	d = timespec_scalar(&delta);
	if (d >= period) {
		overruns = d / period;
		timespec_adds(&current->core.wakeup, &wakeup,
			      overruns * (period + 1));
	} else
		timespec_adds(&current->core.wakeup, &wakeup, period);

	if (overruns)
		ret = -ETIMEDOUT;

	if (overruns_r)
		*overruns_r = overruns;

	return ret;
}

int threadobj_stat(struct threadobj *thobj,
		   struct threadobj_stat *stat) /* thobj->lock held */
{
	struct timespec now, delta;

	__threadobj_check_locked(thobj);

	stat->cpu = sched_getcpu();
	if (stat->cpu < 0)
		stat->cpu = 0;	/* assume uniprocessor on ENOSYS */

	stat->status = threadobj_get_status(thobj);

	if (thobj->run_state & (__THREAD_S_TIMEDWAIT|__THREAD_S_DELAYED)) {
		__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
		timespec_sub(&delta, &thobj->core.timeout, &now);
		stat->timeout = timespec_scalar(&delta);
		/*
		 * The timeout might fire as we are calculating the
		 * delta: sanitize any negative value as 1.
		 */
		if ((sticks_t)stat->timeout < 0)
			stat->timeout = 1;
	} else
		stat->timeout = 0;

	return 0;
}

#endif /* CONFIG_XENO_MERCURY */

void *__threadobj_alloc(size_t tcb_struct_size,
			size_t wait_union_size,
			int thobj_offset)
{
	struct threadobj *thobj;
	void *p;

	if (wait_union_size < sizeof(union copperplate_wait_union))
		wait_union_size = sizeof(union copperplate_wait_union);

	tcb_struct_size = (tcb_struct_size+sizeof(double)-1) & ~(sizeof(double)-1);
	p = xnmalloc(tcb_struct_size + wait_union_size);
	if (p == NULL)
		return NULL;

	thobj = p + thobj_offset;
	thobj->wait_union = p + tcb_struct_size;
	thobj->wait_size = wait_union_size;

	return p;
}

void threadobj_init(struct threadobj *thobj,
		    struct threadobj_init_data *idata)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	thobj->magic = idata->magic;
	thobj->tid = 0;
	thobj->tracer = NULL;
	thobj->wait_sobj = NULL;
	thobj->finalizer = idata->finalizer;
	thobj->schedlock_depth = 0;
	thobj->status = __THREAD_S_WARMUP;
	thobj->run_state = __THREAD_S_DORMANT;
	thobj->priority = idata->priority;
	thobj->policy = idata->priority ? SCHED_RT : SCHED_OTHER;
	holder_init(&thobj->wait_link);
	thobj->cnode = __node_id;
	thobj->pid = 0;
	thobj->cancel_sem = NULL;

	/*
	 * CAUTION: wait_union and wait_size have been set in
	 * __threadobj_alloc(), do not overwrite.
	 */

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute));
	__RT(pthread_mutex_init(&thobj->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));

	__RT(pthread_condattr_init(&cattr));
	__RT(pthread_condattr_setpshared(&cattr, mutex_scope_attribute));
	__RT(pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE));
	__RT(pthread_cond_init(&thobj->barrier, &cattr));
	__RT(pthread_condattr_destroy(&cattr));

	threadobj_init_corespec(thobj);
}

/*
 * NOTE: to spare us the need for passing the equivalent of a
 * syncstate argument to each thread locking operation, we hold the
 * cancel state of the locker directly into the locked thread, prior
 * to disabling cancellation for the calling thread.
 *
 * However, this means that we must save some state information on the
 * stack prior to calling any service which releases that lock
 * implicitly, such as pthread_cond_wait(). Failing to do so would
 * introduce the possibility for the saved state to be overwritten by
 * another thread which managed to grab the lock after
 * pthread_cond_wait() dropped it.
 *
 * XXX: cancel_state is held in the descriptor of the target thread,
 * not the current one, because we allow non-copperplate threads to
 * call these services, and these have no threadobj descriptor.
 */

static int start_sync(struct threadobj *thobj, int mask)
{
	int oldstate, status;

	for (;;) {
		status = thobj->status;
		if (status & mask)
			break;
		oldstate = thobj->cancel_state;
		__threadobj_tag_unlocked(thobj);
		__RT(pthread_cond_wait(&thobj->barrier, &thobj->lock));
		__threadobj_tag_locked(thobj);
		thobj->cancel_state = oldstate;
	}

	return status;
}

void threadobj_start(struct threadobj *thobj)	/* thobj->lock held. */
{
	struct threadobj *current = threadobj_current();

	__threadobj_check_locked(thobj);

	if (thobj->status & __THREAD_S_STARTED)
		return;

	thobj->status |= __THREAD_S_STARTED;
	__RT(pthread_cond_signal(&thobj->barrier));

	if (current && thobj->priority <= current->priority)
		return;
	/*
	 * Caller needs synchronization with the thread being started,
	 * which has higher priority. We shall wait until that thread
	 * enters the user code, or aborts prior to reaching that
	 * point, whichever comes first.
	 */
	start_sync(thobj, __THREAD_S_ACTIVE);
}

void threadobj_shadow(struct threadobj *thobj)
{
	assert(thobj != threadobj_current());
	threadobj_lock(thobj);
	thobj->status |= __THREAD_S_STARTED|__THREAD_S_ACTIVE;
	threadobj_unlock(thobj);
}

void threadobj_wait_start(void) /* current->lock free. */
{
	struct threadobj *current = threadobj_current();
	int status;

	threadobj_lock(current);
	status = start_sync(current, __THREAD_S_STARTED|__THREAD_S_ABORTED);
	threadobj_unlock(current);

	/*
	 * We may have preempted the guy who set __THREAD_S_ABORTED in
	 * our status before it had a chance to issue pthread_cancel()
	 * on us, so we need to go idle into a cancellation point to
	 * wait for it: use pause() for this.
	 */
	while (status & __THREAD_S_ABORTED)
		pause();
}

void threadobj_notify_entry(void) /* current->lock free. */
{
	struct threadobj *current = threadobj_current();

	threadobj_lock(current);
	current->status |= __THREAD_S_ACTIVE;
	current->run_state = __THREAD_S_RUNNING;
	__RT(pthread_cond_signal(&current->barrier));
	threadobj_unlock(current);
}

/* thobj->lock free, cancellation disabled. */
int threadobj_prologue(struct threadobj *thobj, const char *name)
{
	struct threadobj *current = threadobj_current();
	int ret;

	/*
	 * Check whether we overlay the default main TCB we set in
	 * main_overlay(), releasing it if so.
	 */
	if (current) {
		/*
		 * CAUTION: we may not overlay non-default TCB. The
		 * upper API should catch this issue before we get
		 * called.
		 */
		assert(current->magic == 0);
		sysgroup_remove(thread, &current->memspec);
		finalize_thread(current);
		threadobj_cleanup_corespec(current);
		threadobj_free(current);
	} else
		pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	if (name) {
		strncpy(thobj->name, name, sizeof(thobj->name) - 1);
		thobj->name[sizeof(thobj->name) - 1] = '\0';
	} else
		*thobj->name = '\0';

	thobj->errno_pointer = &errno;
	backtrace_init_context(&thobj->btd, name);
	ret = threadobj_setup_corespec(thobj);
	if (ret)
		return __bt(ret);

	threadobj_set_current(thobj);
	thobj->pid = copperplate_get_tid();

	/*
	 * Link the thread to the shared queue, so that sysregd can
	 * retrieve it. Nop if --disable-pshared.
	 */
	sysgroup_add(thread, &thobj->memspec);

	threadobj_lock(thobj);
	thobj->status &= ~__THREAD_S_WARMUP;
	__RT(pthread_cond_signal(&thobj->barrier));
	threadobj_unlock(thobj);

#ifdef CONFIG_XENO_ASYNC_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif
	threadobj_run_corespec(thobj);

	return 0;
}

static void cancel_sync(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int oldstate, ret = 0;
	sem_t *sem;

	__threadobj_check_locked(thobj);

	/*
	 * We have to allocate the cancel sync sema4 in the main heap
	 * dynamically, so that it always live in valid memory when we
	 * wait on it and the cancelled thread posts it. This has to
	 * be true regardless of whether --enable-pshared is in
	 * effect, or thobj becomes stale after the finalizer has run
	 * (we cannot host this sema4 in thobj for this reason).
	 */
	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL)
		ret = -ENOMEM;
	else
		__STD(sem_init(sem, sem_scope_attribute, 0));

	thobj->cancel_sem = sem;

	/*
	 * If the thread to delete is warming up, wait until it
	 * reaches the start barrier before sending the cancellation
	 * signal.
	 */
	while (thobj->status & __THREAD_S_WARMUP) {
		oldstate = thobj->cancel_state;
		__threadobj_tag_unlocked(thobj);
		__RT(pthread_cond_wait(&thobj->barrier, &thobj->lock));
		__threadobj_tag_locked(thobj);
		thobj->cancel_state = oldstate;
	}

	/*
	 * Ok, now we shall raise the abort flag if the thread was not
	 * started yet, to kick it out of the barrier wait. We are
	 * covered by the target thread lock we hold, so we can't race
	 * with threadobj_start().
	 */
	if ((thobj->status & __THREAD_S_STARTED) == 0) {
		thobj->status |= __THREAD_S_ABORTED;
		__RT(pthread_cond_signal(&thobj->barrier));
	}

	threadobj_cancel_corespec(thobj);

	threadobj_unlock(thobj);

	pthread_cancel(tid);

	/*
	 * Not being able to sync up with the cancelled thread is not
	 * considered fatal, despite it's likely bad news for sure, so
	 * that we can keep on cleaning up the mess, hoping for the
	 * best.
	 */
	if (sem == NULL || __STD(sem_wait(sem)))
		warning("cannot sync with thread finalizer, %s",
			symerror(sem ? -errno : ret));
	if (sem) {
		__STD(sem_destroy(sem));
		xnfree(sem);
	}
}

int threadobj_sleep(struct timespec *ts)
{
	struct threadobj *current = threadobj_current();
	int ret;

	/*
	 * clock_nanosleep() returns -EINTR upon threadobj_unblock()
	 * with both Cobalt and Mercury cores.
	 */
	current->run_state = __THREAD_S_DELAYED;
	threadobj_save_timeout(&current->core, ts);
	ret = -__RT(clock_nanosleep(CLOCK_COPPERPLATE, TIMER_ABSTIME, ts, NULL));
	current->run_state = __THREAD_S_RUNNING;

	return ret;
}

/* thobj->lock held on entry, released on return */
int threadobj_cancel(struct threadobj *thobj)
{
	__threadobj_check_locked(thobj);

	/*
	 * This basically makes the thread enter a zombie state, since
	 * it won't be reachable by anyone after its magic has been
	 * trashed.
	 */
	thobj->magic = ~thobj->magic;

	if (thobj == threadobj_current()) {
		threadobj_unlock(thobj);
		pthread_exit(NULL);
	}

	cancel_sync(thobj);

	return 0;
}

static void finalize_thread(void *p) /* thobj->lock free */
{
	struct threadobj *thobj = p;

	if (thobj == NULL || thobj == THREADOBJ_IRQCONTEXT)
		return;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	threadobj_set_current(p);
	thobj->pid = 0;

	if (thobj->wait_sobj)
		__syncobj_cleanup_wait(thobj->wait_sobj, thobj);

	sysgroup_remove(thread, &thobj->memspec);

	if (thobj->tracer)
		traceobj_unwind(thobj->tracer);

	backtrace_dump(&thobj->btd);
	backtrace_destroy_context(&thobj->btd);

	if (thobj->cancel_sem)
		/* Release the killer from threadobj_cancel(). */
		sem_post(thobj->cancel_sem);

	/*
	 * Careful: once the user-defined finalizer has run, thobj may
	 * be laid on stale memory. Do not refer to its contents.
	 */

	if (thobj->finalizer)
		thobj->finalizer(thobj);

	threadobj_set_current(NULL);
}

void threadobj_destroy(struct threadobj *thobj) /* thobj->lock free */
{
	__RT(pthread_cond_destroy(&thobj->barrier));
	__RT(pthread_mutex_destroy(&thobj->lock));
	threadobj_cleanup_corespec(thobj);
}

int threadobj_unblock(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret = 0;

	__threadobj_check_locked(thobj);

	/*
	 * FIXME: racy. We can't assume thobj->wait_sobj is stable.
	 */
	if (thobj->wait_sobj)	/* Remove PEND (+DELAY timeout) */
		syncobj_flush(thobj->wait_sobj);
	else
		/* Remove standalone DELAY */
		ret = -__RT(pthread_kill(tid, SIGRELS));

	return __bt(ret);
}

void threadobj_spin(ticks_t ns)
{
	ticks_t end;

	end = clockobj_get_tsc() + clockobj_ns_to_tsc(ns);
	while (clockobj_get_tsc() < end)
		cpu_relax();
}

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum)
{				/* thobj->lock held */
	__threadobj_check_locked(thobj);

	/*
	 * It makes no sense to enable/disable round-robin while
	 * holding the scheduler lock. Prevent this, which makes our
	 * logic simpler in the Mercury case with respect to tracking
	 * the current scheduling parameters.
	 */
	if (thobj->status & __THREAD_S_NOPREEMPT)
		return -EINVAL;

	return __bt(set_rr(thobj, quantum));
}

#ifdef __XENO_DEBUG__

int __check_cancel_type(const char *locktype)
{
	int oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype);
	if (oldtype != PTHREAD_CANCEL_DEFERRED) {
		warning("%s_nocancel() section is NOT cancel-safe", locktype);
		return __bt(-EINVAL);
	}

	return 0;
}

#endif

static inline void main_overlay(void)
{
	struct threadobj_init_data idata;
	struct threadobj *tcb;

	/*
	 * Make the main() context a basic yet complete thread object,
	 * so that it may use any services which require the caller to
	 * have a Copperplate TCB (e.g. all blocking services).
	 */
	tcb = __threadobj_alloc(sizeof(*tcb),
				sizeof(union copperplate_wait_union),
				0);
	if (tcb == NULL)
		panic("failed to allocate main tcb");

	idata.magic = 0x0;
	idata.finalizer = NULL;
	idata.priority = 0;
	threadobj_init(tcb, &idata);
	tcb->status = __THREAD_S_STARTED|__THREAD_S_ACTIVE;
	threadobj_prologue(tcb, "main");
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
}

void threadobj_pkg_init(void)
{
	threadobj_irq_prio = __RT(sched_get_priority_max(SCHED_RT));
	threadobj_high_prio = threadobj_irq_prio - 1;

	threadobj_init_key();

	pkg_init_corespec();

	main_overlay();
}
