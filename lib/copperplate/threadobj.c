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

#include <sys/time.h>
#include <signal.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sched.h>
#include "copperplate/init.h"
#include "copperplate/panic.h"
#include "copperplate/lock.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/debug.h"

pthread_key_t threadobj_tskey;

int threadobj_min_prio;

int threadobj_max_prio;

int threadobj_async;

static DEFINE_PRIVATE_LIST(thread_list);

static pthread_mutex_t list_lock;

static int global_rr;

static struct timespec global_quantum;

#ifdef CONFIG_XENO_COBALT

static inline void pkg_init_corespec(void)
{
}

static inline void setup_corespec(struct threadobj *thobj)
{
	pthread_set_name_np(pthread_self(), thobj->name);
}

static inline void cleanup_corespec(struct threadobj *thobj)
{
}

int threadobj_cancel(struct threadobj *thobj) /* thobj->lock free */
{
	pthread_t tid;
	int ret;

	if (thobj == threadobj_current())
		pthread_exit(NULL);

	tid = thobj->tid;

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
	ret = __RT(pthread_kill(tid, SIGDEMT));
	if (ret)
		return __bt(-ret);

	ret = __bt(pthread_cancel(tid));
	if (ret)
		return ret;

	return __bt(-pthread_join(tid, NULL));
}

int threadobj_suspend(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret;

	threadobj_unlock(thobj);
	ret = __RT(pthread_kill(tid, SIGSUSP));
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_resume(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret;

	if (thobj == threadobj_current())
		return 0;

	threadobj_unlock(thobj);
	ret = __RT(pthread_kill(tid, SIGRESM));
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_lock_sched(struct threadobj *thobj) /* thobj->lock held */
{
	assert(thobj == threadobj_current());

	if (thobj->schedlock_depth++ > 0)
		return 0;

	thobj->status |= THREADOBJ_SCHEDLOCK;
	/*
	 * In essence, we can't be scheduled out as a result of
	 * locking the scheduler, so no need to drop the thread lock
	 * across this call.
	 */
	return __bt(-pthread_set_mode_np(0, PTHREAD_LOCK_SCHED));
}

int threadobj_unlock_sched(struct threadobj *thobj) /* thobj->lock held */
{
	int ret;

	assert(thobj == threadobj_current());

	/*
	 * Higher layers may not know about the current locking level
	 * and fully rely on us to track it, so we gracefully handle
	 * unbalanced calls here, and let them decide of the outcome
	 * in case of error.
	 */
	if (thobj->schedlock_depth == 0)
		return __bt(-EINVAL);

	if (--thobj->schedlock_depth > 0)
		return 0;

	thobj->status &= ~THREADOBJ_SCHEDLOCK;
	threadobj_unlock(thobj);
	ret = pthread_set_mode_np(PTHREAD_LOCK_SCHED, 0);
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_set_priority(struct threadobj *thobj, int prio) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int ret, policy;

	ret = __RT(pthread_getschedparam(tid, &policy, &param));
	if (ret)
		return __bt(-ret);

	if (param.sched_priority == prio)
		return 0;

	threadobj_unlock(thobj);
	/*
	 * XXX: as a side effect, resetting SCHED_RR will refill the
	 * time credit for the target thread with the last rrperiod
	 * set.
	 */
	param.sched_priority = prio;
	ret = __RT(pthread_setschedparam(tid, policy, &param));
	threadobj_lock(thobj);

	return __bt(-ret);
}

static int set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	struct sched_param_ex xparam;
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int policy, ret;

	ret = __RT(pthread_getschedparam(tid, &policy, &param));
	if (ret)
		return __bt(-ret);

	if (quantum == NULL) {
		xparam.sched_rr_quantum.tv_sec = 0;
		xparam.sched_rr_quantum.tv_nsec = 0;
		thobj->status &= ~THREADOBJ_ROUNDROBIN;
		policy = SCHED_FIFO;
	} else {
		xparam.sched_rr_quantum = *quantum;
		if (quantum->tv_sec == 0 && quantum->tv_nsec == 0) {
			thobj->status &= ~THREADOBJ_ROUNDROBIN;
			policy = SCHED_FIFO;
		} else {
			thobj->status |= THREADOBJ_ROUNDROBIN;
			policy = SCHED_RR;
		}
	}

	xparam.sched_priority = param.sched_priority;
	threadobj_unlock(thobj);
	ret = pthread_setschedparam_ex(tid, policy, &xparam);
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum)
{				/* thobj->lock held if valid */
	int ret;

	if (thobj)
		return __bt(set_rr(thobj, quantum));

	global_rr = (quantum != NULL);
	if (global_rr)
		global_quantum = *quantum;

	/*
	 * XXX: Enable round-robin for all threads locally known by
	 * the current process. Round-robin is most commonly about
	 * having multiple threads getting an equal share of time for
	 * running the same bulk of code, so applying this policy
	 * session-wide to multiple Xenomai processes would not make
	 * much sense. I.e. one is better off having all those threads
	 * running within a single process.
	 */
	ret = 0;
	push_cleanup_lock(&list_lock);
	read_lock(&list_lock);

	if (!pvlist_empty(&thread_list)) {
		pvlist_for_each_entry(thobj, &thread_list, thread_link) {
			threadobj_lock(thobj);
			ret = set_rr(thobj, quantum);
			threadobj_unlock(thobj);
			if (ret)
				break;
		}
	}

	read_unlock(&list_lock);
	pop_cleanup_lock(&list_lock);

	return __bt(ret);
}

int threadobj_start_rr(struct timespec *quantum)
{
	return __bt(threadobj_set_rr(NULL, quantum));
}

void threadobj_stop_rr(void)
{
	threadobj_set_rr(NULL, NULL);
}

int threadobj_set_periodic(struct threadobj *thobj,
			   struct timespec *idate, struct timespec *period)
{
	return __bt(-pthread_make_periodic_np(thobj->tid,
					      CLOCK_COPPERPLATE, idate, period));
}

int threadobj_wait_period(struct threadobj *thobj,
			  unsigned long *overruns_r)
{
	assert(thobj == threadobj_current());
	return __bt(-pthread_wait_np(overruns_r));
}

#else /* CONFIG_XENO_MERCURY */

#include <sys/prctl.h>
#include "copperplate/notifier.h"

/* Private signal used for unblocking from syscalls. */
#define SIGRELS  	(SIGRTMIN + 9)
#define cpu_relax()	do { } while (0)

static void unblock_sighandler(int sig)
{
	/*
	 * nop -- we just want the receiving thread to unblock with
	 * EINTR if applicable.
	 */
}

static inline void pkg_init_corespec(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = unblock_sighandler;
	sigaction(SIGRELS, &sa, NULL);

	notifier_pkg_init();
}

static void notifier_callback(const struct notifier *nf)
{
	struct threadobj *current;

	current = container_of(nf, struct threadobj, core.notifier);
	assert(current == threadobj_current());

	if (current->suspend_hook)
		current->suspend_hook(current, THREADOBJ_SUSPEND);

	notifier_wait(nf); /* Wait for threadobj_resume(). */

	if (current->suspend_hook)
		current->suspend_hook(current, THREADOBJ_RESUME);
}

static inline void setup_corespec(struct threadobj *thobj)
{
	prctl(PR_SET_NAME, (unsigned long)thobj->name, 0, 0, 0);
	notifier_init(&thobj->core.notifier, notifier_callback, 1);
}

static inline void cleanup_corespec(struct threadobj *thobj)
{
	notifier_destroy(&thobj->core.notifier);
}

int threadobj_cancel(struct threadobj *thobj) /* thobj->lock free */
{
	int ret;

	if (thobj == threadobj_current())
		pthread_exit(NULL);

	ret = __bt(-pthread_cancel(thobj->tid));
	if (ret)
		return ret;

	return __bt(-pthread_join(thobj->tid, NULL));
}

int threadobj_suspend(struct threadobj *thobj)
{
	struct notifier *nf = &thobj->core.notifier;
	int ret;

	threadobj_unlock(thobj); /* FIXME: racy */
	ret = notifier_signal(nf);
	threadobj_lock(thobj);

	return __bt(ret);
}

int threadobj_resume(struct threadobj *thobj)
{
	if (thobj == threadobj_current())
		return 0;

	return __bt(notifier_release(&thobj->core.notifier));
}

int threadobj_lock_sched(struct threadobj *thobj)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int policy, ret;

	assert(thobj == threadobj_current());

	if (thobj->schedlock_depth++ > 0)
		return 0;

	ret = __RT(pthread_getschedparam(tid, &policy, &param));
	if (ret)
		return __bt(-ret);

	thobj->core.prio_unlocked = param.sched_priority;
	thobj->status |= THREADOBJ_SCHEDLOCK;
	param.sched_priority = threadobj_max_prio - 1;

	return __bt(-__RT(pthread_setschedparam(tid, SCHED_FIFO, &param)));
}

int threadobj_unlock_sched(struct threadobj *thobj)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int ret;

	assert(thobj == threadobj_current());

	if (thobj->schedlock_depth == 0)
		return __bt(-EINVAL);

	if (--thobj->schedlock_depth > 0)
		return 0;

	thobj->status &= ~THREADOBJ_SCHEDLOCK;
	param.sched_priority = thobj->core.prio_unlocked;
	threadobj_unlock(thobj);
	ret = __RT(pthread_setschedparam(tid, SCHED_FIFO, &param));
	threadobj_lock(thobj);

	return __bt(-ret);
}

int threadobj_set_priority(struct threadobj *thobj, int prio)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int ret;

	/*
	 * We don't actually change the scheduling priority in case
	 * the target thread holds the scheduler lock, but only record
	 * the level to set when unlocking.
	 */
	if (thobj->status & THREADOBJ_SCHEDLOCK) {
		thobj->core.prio_unlocked = prio;
		return 0;
	}

	threadobj_unlock(thobj);
	/*
	 * Since we released the thread container lock, we now rely on
	 * the pthread interface to recheck the tid for existence.
	 */
	param.sched_priority = prio;
	ret = __RT(pthread_setschedparam(tid, SCHED_FIFO, &param));
	threadobj_lock(thobj);

	return __bt(-ret);
}

static void roundrobin_handler(int sig)
{
	struct threadobj *current = threadobj_current();

	/*
	 * We do manual round-robin within SCHED_FIFO to allow for
	 * multiple time slices system-wide.
	 */
	if (current && (current->status & THREADOBJ_ROUNDROBIN))
		__RT(sched_yield());
}

static inline void set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	if (quantum) {
		thobj->status |= THREADOBJ_ROUNDROBIN;
		thobj->core.tslice = *quantum;
	} else
		thobj->status &= ~THREADOBJ_ROUNDROBIN;
}

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	if (thobj) {
		set_rr(thobj, quantum);
		return 0;
	}

	global_rr = (quantum != NULL);
	if (global_rr)
		global_quantum = *quantum;

	/*
	 * XXX: Enable round-robin for all threads locally known by
	 * the current process. Round-robin is most commonly about
	 * having multiple threads getting an equal share of time for
	 * running the same bulk of code, so applying this policy
	 * session-wide to multiple Xenomai processes would not make
	 * much sense. I.e. one is better off having all those threads
	 * running within a single process.
	 */
	push_cleanup_lock(&list_lock);
	read_lock(&list_lock);

	if (!pvlist_empty(&thread_list)) {
		pvlist_for_each_entry(thobj, &thread_list, thread_link) {
			threadobj_lock(thobj);
			set_rr(thobj, quantum);
			threadobj_unlock(thobj);
		}
	}

	read_unlock(&list_lock);
	pop_cleanup_lock(&list_lock);

	return 0;
}

int threadobj_start_rr(struct timespec *quantum)
{
	struct itimerval value, ovalue;
	struct sigaction sa;
	int ret;

	ret = threadobj_set_rr(NULL, quantum);
	if (ret)
		return __bt(ret);

	value.it_interval.tv_sec = quantum->tv_sec;
	value.it_interval.tv_usec = quantum->tv_nsec / 1000;
	
	ret = getitimer(ITIMER_VIRTUAL, &ovalue);
	if (ret == 0 &&
	    value.it_interval.tv_sec == ovalue.it_interval.tv_sec &&
	    value.it_interval.tv_usec == ovalue.it_interval.tv_usec)
		return 0;	/* Already enabled. */

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = roundrobin_handler;
	sigaction(SIGVTALRM, &sa, NULL);

	value.it_value = value.it_interval;
	ret = setitimer(ITIMER_VIRTUAL, &value, NULL);
	if (ret)
		return __bt(-errno);

	return 0;
}

void threadobj_stop_rr(void)
{
	struct itimerval value;
	struct sigaction sa;

	threadobj_set_rr(NULL, NULL);

	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 0;
	value.it_interval = value.it_value;
	
	setitimer(ITIMER_VIRTUAL, &value, NULL);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGVTALRM, &sa, NULL);
}

int threadobj_set_periodic(struct threadobj *thobj,
			   struct timespec *idate, struct timespec *period)
{
	return -ENOSYS;		/* FIXME */
}

int threadobj_wait_period(struct threadobj *thobj,
			  unsigned long *overruns_r)
{
	assert(thobj == threadobj_current());
	return -ENOSYS;		/* FIXME */
}

#endif /* CONFIG_XENO_MERCURY */

void threadobj_init(struct threadobj *thobj,
		    struct threadobj_init_data *idata)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	thobj->magic = idata->magic;
	thobj->tid = 0;
	thobj->tracer = NULL;
	thobj->wait_struct = NULL;
	thobj->finalizer = idata->finalizer;
	thobj->wait_hook = idata->wait_hook;
	thobj->schedlock_depth = 0;
	thobj->status = 0;
	holder_init(&thobj->wait_link);
	thobj->suspend_hook = idata->suspend_hook;
	thobj->cnode = __this_node.id;

	__RT(pthread_condattr_init(&cattr));
	__RT(pthread_condattr_setpshared(&cattr, mutex_scope_attribute));
	__RT(pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE));
	__RT(pthread_cond_init(&thobj->wait_sync, &cattr));
	__RT(pthread_condattr_destroy(&cattr));

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute));
	__RT(pthread_mutex_init(&thobj->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));
}

/* thobj->lock free, cancellation disabled. */
int threadobj_prologue(struct threadobj *thobj, const char *name)
{
	thobj->name = name;
	backtrace_init_context(&thobj->btd, name);
	setup_corespec(thobj);

	write_lock_nocancel(&list_lock);
	pvlist_append(&thobj->thread_link, &thread_list);
	write_unlock(&list_lock);

	thobj->errno_pointer = &errno;
	pthread_setspecific(threadobj_tskey, thobj);

	if (global_rr)
		threadobj_set_rr(thobj, &global_quantum);

#ifdef CONFIG_XENO_ASYNC_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

	return 0;
}

void threadobj_finalize(void *p) /* thobj->lock free */
{
	struct threadobj *thobj = p;

	if (thobj == THREADOBJ_IRQCONTEXT)
		return;

	write_lock_nocancel(&list_lock);
	pvlist_remove(&thobj->thread_link);
	write_unlock(&list_lock);

	cleanup_corespec(thobj);

	if (thobj->tracer)
		traceobj_unwind(thobj->tracer);

	if (thobj->finalizer)
		thobj->finalizer(thobj);

	backtrace_dump(&thobj->btd);
	backtrace_destroy_context(&thobj->btd);
}

void threadobj_destroy(struct threadobj *thobj) /* thobj->lock free */
{
	__RT(pthread_mutex_destroy(&thobj->lock));
}

int threadobj_unblock(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret = 0;

	if (thobj->wait_sobj)	/* Remove PEND (+DELAY timeout) */
		syncobj_flush(thobj->wait_sobj, SYNCOBJ_FLUSHED);
	else
		/* Remove standalone DELAY */
		ret = -__RT(pthread_kill(tid, SIGRELS));

	return __bt(ret);
}

int threadobj_get_priority(struct threadobj *thobj) /* thobj->lock held */
{
	struct sched_param param;
	int ret, policy;

	ret = __RT(pthread_getschedparam(thobj->tid, &policy, &param));
	if (ret)
		return __bt(-ret);

	return param.sched_priority;
}

void threadobj_spin(ticks_t ns)
{
	ticks_t end;

	end = clockobj_get_tsc() + clockobj_ns_to_tsc(ns);
	while (clockobj_get_tsc() < end)
		cpu_relax();
}

void threadobj_pkg_init(void)
{
	threadobj_max_prio = sched_get_priority_max(SCHED_FIFO);
	threadobj_min_prio = sched_get_priority_min(SCHED_FIFO);
	threadobj_async = 0;

	/* PI and recursion would be overkill. */
	__RT(pthread_mutex_init(&list_lock, NULL));

	if (pthread_key_create(&threadobj_tskey, threadobj_finalize) != 0)
		panic("failed to allocate TSD key");

	pkg_init_corespec();
}
