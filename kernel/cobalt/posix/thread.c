/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

/**
 * @ingroup cobalt
 * @defgroup cobalt_thread Threads management services.
 *
 * Threads management services.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_09.html#tag_02_09">
 * Specification.</a>
 *
 *@{*/

#include <linux/types.h>
#include <linux/cred.h>
#include <linux/jhash.h>
#include <linux/signal.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <cobalt/uapi/signal.h>
#include "internal.h"
#include "thread.h"
#include "signal.h"
#include "timer.h"
#include "clock.h"
#include "sem.h"
#define CREATE_TRACE_POINTS
#include <trace/events/cobalt-posix.h>

xnticks_t cobalt_time_slice;

#define PTHREAD_HSLOTS (1 << 8)	/* Must be a power of 2 */

/* Process-local index, pthread_t x mm_struct (cobalt_local_hkey). */
struct local_thread_hash {
	pid_t pid;
	struct cobalt_thread *thread;
	struct cobalt_local_hkey hkey;
	struct local_thread_hash *next;
};

/* System-wide index on task_struct->pid. */
struct global_thread_hash {
	pid_t pid;
	struct cobalt_thread *thread;
	struct global_thread_hash *next;
};

static struct local_thread_hash *local_index[PTHREAD_HSLOTS];

static struct global_thread_hash *global_index[PTHREAD_HSLOTS];

static inline struct local_thread_hash *
thread_hash(const struct cobalt_local_hkey *hkey,
	    struct cobalt_thread *thread, pid_t pid)
{
	struct global_thread_hash **ghead, *gslot;
	struct local_thread_hash **lhead, *lslot;
	u32 hash;
	void *p;
	spl_t s;

	p = xnmalloc(sizeof(*lslot) + sizeof(*gslot));
	if (p == NULL)
		return NULL;

	lslot = p;
	lslot->hkey = *hkey;
	lslot->thread = thread;
	lslot->pid = pid;
	hash = jhash2((u32 *)&lslot->hkey,
		      sizeof(lslot->hkey) / sizeof(u32), 0);
	lhead = &local_index[hash & (PTHREAD_HSLOTS - 1)];

	gslot = p + sizeof(*lslot);
	gslot->pid = pid;
	gslot->thread = thread;
	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);
	ghead = &global_index[hash & (PTHREAD_HSLOTS - 1)];

	xnlock_get_irqsave(&nklock, s);
	lslot->next = *lhead;
	*lhead = lslot;
	gslot->next = *ghead;
	*ghead = gslot;
	xnlock_put_irqrestore(&nklock, s);

	return lslot;
}

static inline void thread_unhash(const struct cobalt_local_hkey *hkey)
{
	struct global_thread_hash **gtail, *gslot;
	struct local_thread_hash **ltail, *lslot;
	pid_t pid;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *) hkey, sizeof(*hkey) / sizeof(u32), 0);
	ltail = &local_index[hash & (PTHREAD_HSLOTS - 1)];

	xnlock_get_irqsave(&nklock, s);

	lslot = *ltail;
	while (lslot &&
	       (lslot->hkey.u_pth != hkey->u_pth ||
		lslot->hkey.mm != hkey->mm)) {
		ltail = &lslot->next;
		lslot = *ltail;
	}

	if (lslot == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return;
	}

	*ltail = lslot->next;
	pid = lslot->pid;
	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);
	gtail = &global_index[hash & (PTHREAD_HSLOTS - 1)];
	gslot = *gtail;
	while (gslot && gslot->pid != pid) {
		gtail = &gslot->next;
		gslot = *gtail;
	}
	/* gslot must be found here. */
	XENO_BUGON(COBALT, !(gslot && gtail));
	*gtail = gslot->next;

	xnlock_put_irqrestore(&nklock, s);

	xnfree(lslot);
	xnfree(gslot);
}

static struct cobalt_thread *
thread_lookup(const struct cobalt_local_hkey *hkey)
{
	struct local_thread_hash *lslot;
	struct cobalt_thread *thread;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *)hkey, sizeof(*hkey) / sizeof(u32), 0);
	lslot = local_index[hash & (PTHREAD_HSLOTS - 1)];

	xnlock_get_irqsave(&nklock, s);

	while (lslot != NULL &&
	       (lslot->hkey.u_pth != hkey->u_pth || lslot->hkey.mm != hkey->mm))
		lslot = lslot->next;

	thread = lslot ? lslot->thread : NULL;

	xnlock_put_irqrestore(&nklock, s);

	return thread;
}

struct cobalt_thread *cobalt_thread_find(pid_t pid) /* nklocked, IRQs off */
{
	struct global_thread_hash *gslot;
	u32 hash;

	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);

	gslot = global_index[hash & (PTHREAD_HSLOTS - 1)];
	while (gslot && gslot->pid != pid)
		gslot = gslot->next;

	return gslot ? gslot->thread : NULL;
}
EXPORT_SYMBOL_GPL(cobalt_thread_find);

struct cobalt_thread *cobalt_thread_find_local(pid_t pid) /* nklocked, IRQs off */
{
	struct cobalt_thread *thread;

	thread = cobalt_thread_find(pid);
	if (thread == NULL || thread->hkey.mm != current->mm)
		return NULL;

	return thread;
}
EXPORT_SYMBOL_GPL(cobalt_thread_find_local);

struct cobalt_thread *cobalt_thread_lookup(unsigned long pth) /* nklocked, IRQs off */
{
	struct cobalt_local_hkey hkey;

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	return thread_lookup(&hkey);
}
EXPORT_SYMBOL_GPL(cobalt_thread_lookup);

void cobalt_thread_map(struct xnthread *curr)
{
	struct cobalt_thread *thread;

	thread = container_of(curr, struct cobalt_thread, threadbase);
	thread->process = cobalt_process_context();
	XENO_BUGON(NUCLEUS, thread->process == NULL);
}

struct xnpersonality *cobalt_thread_exit(struct xnthread *curr)
{
	struct cobalt_thread *thread;
	spl_t s;

	thread = container_of(curr, struct cobalt_thread, threadbase);
	/*
	 * Unhash first, to prevent further access to the TCB from
	 * userland.
	 */
	thread_unhash(&thread->hkey);
	xnlock_get_irqsave(&nklock, s);
	cobalt_mark_deleted(thread);
	list_del(&thread->link);
	xnlock_put_irqrestore(&nklock, s);
	cobalt_signal_flush(thread);
	xnsynch_destroy(&thread->monitor_synch);
	xnsynch_destroy(&thread->sigwait);

	return NULL;
}

struct xnpersonality *cobalt_thread_finalize(struct xnthread *zombie)
{
	struct cobalt_thread *thread;

	thread = container_of(zombie, struct cobalt_thread, threadbase);
	xnfree(thread);

	return NULL;
}

static struct xnsched_class *
get_policy_param(union xnsched_policy_param *param,
		 int u_policy, const struct sched_param_ex *param_ex,
		 xnticks_t *tslice_r)
{
	struct xnsched_class *sched_class;
	int prio, policy;
	xnticks_t tslice;

	prio = param_ex->sched_priority;
	tslice = XN_INFINITE;
	policy = u_policy;

	/*
	 * NOTE: The user-defined policy may be different than ours,
	 * e.g. SCHED_FIFO,prio=-7 from userland would be interpreted
	 * as SCHED_WEAK,prio=7 in kernel space.
	 */
	if (prio < 0) {
		prio = -prio;
		policy = SCHED_WEAK;
	}
	sched_class = &xnsched_class_rt;
	param->rt.prio = prio;

	switch (policy) {
	case SCHED_NORMAL:
		if (prio)
			return NULL;
		/*
		 * When the weak scheduling class is compiled in,
		 * SCHED_WEAK and SCHED_NORMAL threads are scheduled
		 * by xnsched_class_weak, at their respective priority
		 * levels. Otherwise, SCHED_NORMAL is scheduled by
		 * xnsched_class_rt at priority level #0.
		 */
	case SCHED_WEAK:
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
		if (prio < XNSCHED_WEAK_MIN_PRIO ||
		    prio > XNSCHED_WEAK_MAX_PRIO)
			return NULL;
		param->weak.prio = prio;
		sched_class = &xnsched_class_weak;
#else
		if (prio)
			return NULL;
#endif
		break;
	case SCHED_RR:
		/* if unspecified, use current one. */
		tslice = ts2ns(&param_ex->sched_rr_quantum);
		if (tslice == XN_INFINITE)
			tslice = *tslice_r;
		/* falldown wanted */
	case SCHED_FIFO:
		if (prio < XNSCHED_FIFO_MIN_PRIO ||
		    prio > XNSCHED_FIFO_MAX_PRIO)
			return NULL;
		break;
	case SCHED_COBALT:
		if (prio < XNSCHED_RT_MIN_PRIO ||
		    prio > XNSCHED_RT_MAX_PRIO)
			return NULL;
		break;
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	case SCHED_SPORADIC:
		param->pss.normal_prio = param_ex->sched_priority;
		param->pss.low_prio = param_ex->sched_ss_low_priority;
		param->pss.current_prio = param->pss.normal_prio;
		param->pss.init_budget = ts2ns(&param_ex->sched_ss_init_budget);
		param->pss.repl_period = ts2ns(&param_ex->sched_ss_repl_period);
		param->pss.max_repl = param_ex->sched_ss_max_repl;
		sched_class = &xnsched_class_sporadic;
		break;
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	case SCHED_TP:
		param->tp.prio = param_ex->sched_priority;
		param->tp.ptid = param_ex->sched_tp_partition;
		sched_class = &xnsched_class_tp;
		break;
#endif
#ifdef CONFIG_XENO_OPT_SCHED_QUOTA
	case SCHED_QUOTA:
		param->quota.prio = param_ex->sched_priority;
		param->quota.tgid = param_ex->sched_quota_group;
		sched_class = &xnsched_class_quota;
		break;
#endif
	default:
		return NULL;
	}

	*tslice_r = tslice;

	return sched_class;
}

/**
 * Set the extended scheduling policy and parameters of the specified
 * thread.
 *
 * This service is an extended version of the regular
 * pthread_setschedparam() service, which supports Xenomai-specific or
 * additional scheduling policies, not available with the host Linux
 * environment.
 *
 * This service set the scheduling policy of the Xenomai thread @a
 * thread to the value @a policy, and its scheduling parameters
 * (e.g. its priority) to the value pointed to by @a param_ex.
 *
 * If @a thread does not match the identifier of a Xenomai thread, this
 * action falls back to the regular pthread_setschedparam() service.
 *
 * @param thread target Cobalt thread;
 *
 * @param policy scheduling policy, one of SCHED_WEAK, SCHED_FIFO,
 * SCHED_COBALT, SCHED_RR, SCHED_SPORADIC, SCHED_TP, SCHED_QUOTA or
 * SCHED_NORMAL;
 *
 * @param param_ex scheduling parameters address. As a special
 * exception, a negative sched_priority value is interpreted as if
 * SCHED_WEAK was given in @a policy, using the absolute value of this
 * parameter as the weak priority level.
 *
 * When CONFIG_XENO_OPT_SCHED_WEAK is enabled, SCHED_WEAK exhibits
 * priority levels in the [0..99] range (inclusive). Otherwise,
 * sched_priority must be zero for the SCHED_WEAK policy.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - EINVAL, @a policy or @a param_ex->sched_priority is invalid;
 * - EAGAIN, in user-space, insufficient memory exists in the system heap,
 *   increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EFAULT, in user-space, @a param_ex is an invalid address;
 * - EPERM, in user-space, the calling process does not have superuser
 *   permissions.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_setschedparam.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating or shadowing a Xenomai thread for the first time in
 * user-space, Xenomai installs a handler for the SIGSHADOW signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGSHADOW signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGSHADOW after
 * creating or shadowing the first Xenomai thread, you have to
 * explicitly call the function cobalt_sigshadow_handler at the
 * beginning of your signal handler, using its return to know if the
 * signal was in fact an internal signal of Xenomai (in which case it
 * returns 1), or if you should handle the signal (in which case it
 * returns 0). cobalt_sigshadow_handler prototype is:
 *
 * <b>int cobalt_sigshadow_handler(int sig, struct siginfo *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction,
 * using the SA_SIGINFO flag, and pass all the arguments you received
 * to cobalt_sigshadow_handler.
 *
 * pthread_setschedparam_ex() may switch the caller to secondary mode.
 */
static inline int
pthread_setschedparam_ex(struct cobalt_thread *thread,
			 int policy, const struct sched_param_ex *param_ex)
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	xnticks_t tslice;
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC,
			       struct cobalt_thread)) {
		ret = -ESRCH;
		goto out;
	}

	tslice = xnthread_time_slice(&thread->threadbase);
	sched_class = get_policy_param(&param, policy, param_ex, &tslice);
	if (sched_class == NULL) {
		ret = -EINVAL;
		goto out;
	}
	thread->sched_u_policy = policy;
	xnthread_set_slice(&thread->threadbase, tslice);
	if (cobalt_call_extension(thread_setsched, &thread->extref, ret,
				  sched_class, &param) && ret)
		goto out;
	xnthread_set_schedparam(&thread->threadbase, sched_class, &param);
	xnsched_run();
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Get the extended scheduling policy and parameters of the specified
 * thread.
 *
 * This service is an extended version of the regular
 * pthread_getschedparam() service, which also supports
 * Xenomai-specific or additional POSIX scheduling policies, not
 * available with the host Linux environment.
 *
 * @param thread target thread;
 *
 * @param policy_r address where the scheduling policy of @a thread is stored on
 * success;
 *
 * @param param_ex address where the scheduling parameters of @a thread are
 * stored on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 *
 */
static inline int
pthread_getschedparam_ex(struct cobalt_thread *thread,
			 int *policy_r, struct sched_param_ex *param_ex)
{
	struct xnsched_class *base_class;
	struct xnthread *base_thread;
	int prio;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC,
			       struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	base_thread = &thread->threadbase;
	base_class = xnthread_base_class(base_thread);
	*policy_r = thread->sched_u_policy;
	prio = xnthread_base_priority(base_thread);
	param_ex->sched_priority = prio;

	if (base_class == &xnsched_class_rt) {
		if (xnthread_test_state(base_thread, XNRRB))
			ns2ts(&param_ex->sched_rr_quantum, xnthread_time_slice(base_thread));
		goto unlock_and_exit;
	}

#ifdef CONFIG_XENO_OPT_SCHED_WEAK
	if (base_class == &xnsched_class_weak) {
		if (*policy_r != SCHED_WEAK)
			param_ex->sched_priority = -param_ex->sched_priority;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	if (base_class == &xnsched_class_sporadic) {
		param_ex->sched_ss_low_priority = base_thread->pss->param.low_prio;
		ns2ts(&param_ex->sched_ss_repl_period, base_thread->pss->param.repl_period);
		ns2ts(&param_ex->sched_ss_init_budget, base_thread->pss->param.init_budget);
		param_ex->sched_ss_max_repl = base_thread->pss->param.max_repl;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	if (base_class == &xnsched_class_tp) {
		param_ex->sched_tp_partition =
			base_thread->tps - base_thread->sched->tp.partitions;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_QUOTA
	if (base_class == &xnsched_class_quota) {
		param_ex->sched_quota_group = base_thread->quota->tgid;
		goto unlock_and_exit;
	}
#endif

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Create a thread.
 *
 * This service creates a Cobalt thread control block. The created
 * thread may use Cobalt API services.
 *
 * The new thread control block can be mapped over a regular Linux
 * thread, forming a Xenomai shadow.
 *
 * The new thread signal mask is inherited from the current thread, if it was
 * also created with pthread_create(), otherwise the new thread signal mask is
 * empty.
 *
 * Other attributes of the new thread depend on the @a attr
 * argument. If @a attr is null, default values for these attributes
 * are used.
 *
 * Returning from the @a start routine has the same effect as calling
 * pthread_exit() with the return value.
 *
 * @param thread_p address where the identifier of the new thread will be stored on
 * success;
 *
 * @param attr thread attributes;
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a attr is invalid;
 * - EAGAIN, insufficient memory exists in the system heap to create a new
 *   thread, increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EINVAL, thread attribute @a inheritsched is set to PTHREAD_INHERIT_SCHED
 *   and the calling thread does not belong to the Cobalt interface;
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_create.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating or shadowing a Xenomai thread for the first time in
 * user-space, Xenomai installs a handler for the SIGSHADOW signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGSHADOW signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGSHADOW after
 * creating or shadowing the first Xenomai thread, you have to
 * explicitly call the function cobalt_sigshadow_handler at the beginning
 * of your signal handler, using its return to know if the signal was
 * in fact an internal signal of Xenomai (in which case it returns 1),
 * or if you should handle the signal (in which case it returns
 * 0). cobalt_sigshadow_handler prototype is:
 *
 * <b>int cobalt_sigshadow_handler(int sig, struct siginfo *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction,
 * using the SA_SIGINFO flag, and pass all the arguments you received
 * to cobalt_sigshadow_handler.
 */
static inline int pthread_create(struct cobalt_thread **thread_p,
				 int policy,
				 const struct sched_param_ex *param_ex,
				 struct task_struct *task)
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	struct xnthread_init_attr iattr;
	struct cobalt_thread *thread;
	xnticks_t tslice;
	int ret, n;
	spl_t s;

	thread = xnmalloc(sizeof(*thread));
	if (thread == NULL)
		return -EAGAIN;

	tslice = cobalt_time_slice;
	sched_class = get_policy_param(&param, policy, param_ex, &tslice);
	if (sched_class == NULL) {
		xnfree(thread);
		return -EINVAL;
	}

	iattr.name = task->comm;
	iattr.flags = XNUSER|XNFPU;
	iattr.personality = &cobalt_personality;
	iattr.affinity = CPU_MASK_ALL;
	if (xnthread_init(&thread->threadbase,
			  &iattr, sched_class, &param) != 0) {
		xnfree(thread);
		return -EAGAIN;
	}

	thread->sched_u_policy = policy;
	thread->magic = COBALT_THREAD_MAGIC;
	xnsynch_init(&thread->monitor_synch, XNSYNCH_FIFO, NULL);

	xnsynch_init(&thread->sigwait, XNSYNCH_FIFO, NULL);
	sigemptyset(&thread->sigpending);
	for (n = 0; n < _NSIG; n++)
		INIT_LIST_HEAD(thread->sigqueues + n);

	xnthread_set_slice(&thread->threadbase, tslice);
	cobalt_set_extref(&thread->extref, NULL, NULL);

	/*
	 * We need an anonymous registry entry to obtain a handle for
	 * fast mutex locking.
	 */
	ret = xnthread_register(&thread->threadbase, "");
	if (ret) {
		xnsynch_destroy(&thread->monitor_synch);
		xnsynch_destroy(&thread->sigwait);
		xnfree(thread);
		return ret;
	}

	xnlock_get_irqsave(&nklock, s);
	thread->container = &cobalt_kqueues(0)->threadq;
	list_add_tail(&thread->link, thread->container);
	xnlock_put_irqrestore(&nklock, s);

	thread->hkey.u_pth = 0;
	thread->hkey.mm = NULL;

	*thread_p = thread;

	return 0;
}

/**
 * Make a thread periodic.
 *
 * This service make the Cobalt interface @a thread periodic.
 *
 * This service is a non-portable extension of the POSIX interface.
 *
 * @param thread thread identifier. This thread is immediately delayed
 * until the first periodic release point is reached.
 *
 * @param clock_id clock identifier, either CLOCK_REALTIME,
 * CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW.
 *
 * @param starttp start time, expressed as an absolute value of the
 * clock @a clock_id. The affected thread will be delayed until this
 * point is reached.
 *
 * @param periodtp period, expressed as a time interval.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - ETIMEDOUT, the start time has already passed.
 * - EINVAL, the specified clock is unsupported;
 *
 * Rescheduling: always, until the @a starttp start time has been reached.
 */
static inline int pthread_make_periodic_np(struct cobalt_thread *thread,
					   clockid_t clock_id,
					   struct timespec *starttp,
					   struct timespec *periodtp)
{

	xnticks_t start, period;
	int ret;
	spl_t s;

	if (clock_id != CLOCK_MONOTONIC &&
	    clock_id != CLOCK_MONOTONIC_RAW &&
	    clock_id != CLOCK_REALTIME)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC,
			       struct cobalt_thread)) {
		ret = -ESRCH;
		goto unlock_and_exit;
	}

	start = ts2ns(starttp);
	period = ts2ns(periodtp);
	ret = xnthread_set_periodic(&thread->threadbase, start,
				    clock_flag(TIMER_ABSTIME, clock_id),
				    period);
      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Set the mode of the current thread.
 *
 * This service sets the mode of the calling thread. @a clrmask and @a setmask
 * are two bit masks which are respectively cleared and set in the calling
 * thread status. They are a bitwise OR of the following values:
 * - PTHREAD_LOCK_SCHED, when set, locks the scheduler, which prevents the
 *   current thread from being switched out until the scheduler
 *   is unlocked;
 * - PTHREAD_WARNSW, when set, causes the signal SIGXCPU to be sent to the
 *   current thread, whenever it involontary switches to secondary mode;
 * - PTHREAD_CONFORMING can be passed in @a setmask to switch the
 * current user-space task to its preferred runtime mode. The only
 * meaningful use of this switch is to force a real-time shadow back
 * to primary mode. Any other use leads to a nop.
 * - PTHREAD_DISABLE_LOCKBREAK disallows breaking the scheduler
 * lock. In the default case, a thread which holds the scheduler lock
 * is allowed to drop it temporarily for sleeping. If this mode bit is set,
 * such thread would return with EINTR immediately from any blocking call.
 *
 * PTHREAD_LOCK_SCHED and PTHREAD_DISABLE_LOCKBREAK are valid for any
 * Xenomai thread, other bits are valid for Xenomai user-space threads
 * only.
 *
 * This service is a non-portable extension of the POSIX interface.
 *
 * @param clrmask set of bits to be cleared;
 *
 * @param setmask set of bits to be set.
 *
 * @param mode_r If non-NULL, @a mode_r must be a pointer to a memory
 * location which will be written upon success with the previous set
 * of active mode bits. If NULL, the previous set of active mode bits
 * will not be returned.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, some bit in @a clrmask or @a setmask is invalid.
 *
 * @note Setting @a clrmask and @a setmask to zero leads to a nop,
 * only returning the previous mode if @a mode_r is a valid address.
 */
static inline int pthread_set_mode_np(int clrmask, int setmask, int *mode_r)
{
	const int valid_flags = XNLOCK|XNTRAPSW|XNTRAPLB;
	struct xnthread *curr = xnshadow_current();
	int old;

	/*
	 * The conforming mode bit is actually zero, since jumping to
	 * this code entailed switching to the proper mode already.
	 */
	if ((clrmask & ~valid_flags) != 0 || (setmask & ~valid_flags) != 0)
		return -EINVAL;

	old = xnthread_set_mode(curr, clrmask, setmask);
	if (mode_r)
		*mode_r = old;

	if ((clrmask & ~setmask) & XNLOCK)
		/* Reschedule if the scheduler has been unlocked. */
		xnsched_run();

	return 0;
}

/*
 * NOTE: there is no cobalt_thread_setschedparam syscall defined by
 * the Cobalt ABI. Useland changes scheduling parameters only via the
 * extended cobalt_thread_setschedparam_ex syscall.
 */
int cobalt_thread_setschedparam_ex(unsigned long pth,
				   int policy,
				   struct sched_param_ex __user *u_param,
				   unsigned long __user *u_window_offset,
				   int __user *u_promoted)
{
	struct sched_param_ex param_ex;
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	int ret, promoted = 0;

	if (__xn_safe_copy_from_user(&param_ex, u_param, sizeof(param_ex)))
		return -EFAULT;

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	trace_cobalt_pthread_setschedparam(pth, policy, &param_ex);

	thread = thread_lookup(&hkey);
	if (thread == NULL && u_window_offset) {
		thread = cobalt_thread_shadow(current, &hkey, u_window_offset);
		if (IS_ERR(thread))
			return PTR_ERR(thread);

		promoted = 1;
	}

	if (thread)
		ret = pthread_setschedparam_ex(thread, policy, &param_ex);
	else
		ret = -EPERM;

	if (ret == 0 &&
	    __xn_safe_copy_to_user(u_promoted, &promoted, sizeof(promoted)))
		ret = -EFAULT;

	return ret;
}

/*
 * NOTE: there is no cobalt_thread_getschedparam syscall defined by
 * the Cobalt ABI. Useland retrieves scheduling parameters only via
 * the extended cobalt_thread_getschedparam_ex syscall.
 */
int cobalt_thread_getschedparam_ex(unsigned long pth,
				   int __user *u_policy,
				   struct sched_param_ex __user *u_param)
{
	struct sched_param_ex param_ex;
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	int policy, ret;

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_lookup(&hkey);
	if (thread == NULL)
		return -ESRCH;

	ret = pthread_getschedparam_ex(thread, &policy, &param_ex);
	if (ret)
		return ret;

	trace_cobalt_pthread_getschedparam(pth, policy, &param_ex);

	if (__xn_safe_copy_to_user(u_policy, &policy, sizeof(int)))
		return -EFAULT;

	return __xn_safe_copy_to_user(u_param, &param_ex, sizeof(param_ex));
}

/*
 * We want to keep the native pthread_t token unmodified for Xenomai
 * mapped threads, and keep it pointing at a genuine NPTL/LinuxThreads
 * descriptor, so that portions of the standard POSIX interface which
 * are not overriden by Xenomai fall back to the original Linux
 * services.
 *
 * If the latter invoke Linux system calls, the associated shadow
 * thread will simply switch to secondary exec mode to perform
 * them. For this reason, we need an external index to map regular
 * pthread_t values to Xenomai's internal thread ids used in
 * syscalling the Cobalt interface, so that the outer interface can
 * keep on using the former transparently.
 *
 * Semaphores and mutexes do not have this constraint, since we fully
 * override their respective interfaces with Xenomai-based
 * replacements.
 */

int cobalt_thread_create(unsigned long pth, int policy,
			 struct sched_param_ex __user *u_param,
			 int shifted_muxid,
			 unsigned long __user *u_window_offset)
{
	struct cobalt_thread *thread = NULL;
	struct task_struct *p = current;
	struct sched_param_ex param_ex;
	struct cobalt_local_hkey hkey;
	int ret, muxid;

	if (__xn_safe_copy_from_user(&param_ex, u_param, sizeof(param_ex)))
		return -EFAULT;

	trace_cobalt_pthread_create(pth, policy, &param_ex);

	/*
	 * We have been passed the pthread_t identifier the user-space
	 * Cobalt library has assigned to our caller; we'll index our
	 * internal pthread_t descriptor in kernel space on it.
	 */
	hkey.u_pth = pth;
	hkey.mm = p->mm;

	ret = pthread_create(&thread, policy, &param_ex, p);
	if (ret)
		return ret;

	ret = xnshadow_map_user(&thread->threadbase, u_window_offset);
	if (ret)
		goto fail;

	if (!thread_hash(&hkey, thread, task_pid_vnr(p))) {
		ret = -EAGAIN;
		goto fail;
	}

	thread->hkey = hkey;

	muxid = __xn_mux_unshifted_id(shifted_muxid);
	if (muxid > 0 && xnshadow_push_personality(muxid) == NULL) {
		ret = -EINVAL;
		goto fail;
	}

	return xnshadow_harden();
fail:
	xnthread_cancel(&thread->threadbase);

	return ret;
}

struct cobalt_thread *
cobalt_thread_shadow(struct task_struct *p,
		     struct cobalt_local_hkey *hkey,
		     unsigned long __user *u_window_offset)
{
	struct cobalt_thread *thread = NULL;
	struct sched_param_ex param_ex;
	int ret;

	param_ex.sched_priority = 0;
	trace_cobalt_pthread_create(hkey->u_pth, SCHED_NORMAL, &param_ex);
	ret = pthread_create(&thread, SCHED_NORMAL, &param_ex, p);
	if (ret)
		return ERR_PTR(-ret);

	ret = xnshadow_map_user(&thread->threadbase, u_window_offset);
	if (ret)
		goto fail;

	if (!thread_hash(hkey, thread, task_pid_vnr(p))) {
		ret = -EAGAIN;
		goto fail;
	}

	thread->hkey = *hkey;

	xnshadow_harden();

	return thread;
fail:
	xnthread_cancel(&thread->threadbase);

	return ERR_PTR(ret);
}

int cobalt_thread_make_periodic_np(unsigned long pth,
				   clockid_t clk_id,
				   struct timespec __user *u_startt,
				   struct timespec __user *u_periodt)
{
	struct timespec startt, periodt;
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_lookup(&hkey);

	if (__xn_safe_copy_from_user(&startt, u_startt, sizeof(startt)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&periodt, u_periodt, sizeof(periodt)))
		return -EFAULT;

	trace_cobalt_pthread_make_periodic(pth, clk_id, &startt, &periodt);

	return pthread_make_periodic_np(thread, clk_id, &startt, &periodt);
}

int cobalt_thread_wait_np(unsigned long __user *u_overruns)
{
	unsigned long overruns = 0;
	int ret;

	trace_cobalt_pthread_wait_entry(0);

	ret = xnthread_wait_period(&overruns);
	if (u_overruns && (ret == 0 || ret == -ETIMEDOUT))
		__xn_put_user(overruns, u_overruns);

	trace_cobalt_pthread_wait_exit(ret, overruns);

	return ret;
}

int cobalt_thread_set_mode_np(int clrmask, int setmask, int __user *u_mode_r)
{
	int ret, old;

	trace_cobalt_pthread_set_mode(clrmask, setmask);

	ret = pthread_set_mode_np(clrmask, setmask, &old);
	if (ret)
		return ret;

	if (u_mode_r && __xn_safe_copy_to_user(u_mode_r, &old, sizeof(old)))
		return -EFAULT;

	return 0;
}

int cobalt_thread_set_name_np(unsigned long pth, const char __user *u_name)
{
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	char name[XNOBJECT_NAME_LEN];
	struct task_struct *p;
	spl_t s;

	if (__xn_safe_strncpy_from_user(name, u_name,
					sizeof(name) - 1) < 0)
		return -EFAULT;

	name[sizeof(name) - 1] = '\0';
	hkey.u_pth = pth;
	hkey.mm = current->mm;

	trace_cobalt_pthread_set_name(pth, name);

	xnlock_get_irqsave(&nklock, s);

	thread = thread_lookup(&hkey);
	if (thread == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	ksformat(xnthread_name(&thread->threadbase),
		 XNOBJECT_NAME_LEN - 1, "%s", name);
	p = xnthread_host_task(&thread->threadbase);
	get_task_struct(p);

	xnlock_put_irqrestore(&nklock, s);

	knamecpy(p->comm, name);
	put_task_struct(p);

	return 0;
}

int cobalt_thread_probe_np(pid_t pid)
{
	struct cobalt_thread *thread;
	int ret = 0;
	spl_t s;

	trace_cobalt_pthread_probe(pid);

	xnlock_get_irqsave(&nklock, s);

	if (cobalt_thread_find(pid) == NULL)
		ret = -ESRCH;

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_thread_kill(unsigned long pth, int sig)
{
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	int ret;
	spl_t s;

	trace_cobalt_pthread_kill(pth, sig);

	xnlock_get_irqsave(&nklock, s);

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_lookup(&hkey);
	if (thread == NULL)
		ret = -ESRCH;
	else
		ret = __cobalt_kill(thread, sig, 0);

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_thread_join(unsigned long pth)
{
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	spl_t s;

	trace_cobalt_pthread_join(pth);

	xnlock_get_irqsave(&nklock, s);

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_lookup(&hkey);

	xnlock_put_irqrestore(&nklock, s);

	if (thread == NULL)
		return -ESRCH;

	return xnthread_join(&thread->threadbase, false);
}

int cobalt_thread_stat(pid_t pid,
		       struct cobalt_threadstat __user *u_stat)
{
	struct cobalt_threadstat stat;
	struct cobalt_thread *p;
	struct xnthread *thread;
	xnticks_t xtime;
	spl_t s;

	trace_cobalt_pthread_stat(pid);

	if (pid == 0) {
		thread = xnshadow_current();
		if (thread == NULL)
			return -EPERM;
		xnlock_get_irqsave(&nklock, s);
	} else {
		xnlock_get_irqsave(&nklock, s);
		p = cobalt_thread_find(pid);
		if (p == NULL) {
			xnlock_put_irqrestore(&nklock, s);
			return -ESRCH;
		}
		thread = &p->threadbase;
	}

	/*
	 * We have to hold the nklock to keep most values consistent.
	 */
	stat.cpu = xnsched_cpu(thread->sched);
	xtime = xnthread_get_exectime(thread);
	if (xnthread_sched(thread)->curr == thread)
		xtime += xnstat_exectime_now() - xnthread_get_lastswitch(thread);
	stat.xtime = xnclock_ticks_to_ns(&nkclock, xtime);
	stat.msw = xnstat_counter_get(&thread->stat.ssw);
	stat.csw = xnstat_counter_get(&thread->stat.csw);
	stat.xsc = xnstat_counter_get(&thread->stat.xsc);
	stat.pf = xnstat_counter_get(&thread->stat.pf);
	stat.status = xnthread_state_flags(thread);
	stat.timeout = xnthread_get_timeout(thread,
					    xnclock_read_monotonic(&nkclock));
	strcpy(stat.name, xnthread_name(thread));
	strcpy(stat.personality, xnthread_personality(thread)->name);
	xnlock_put_irqrestore(&nklock, s);

	return __xn_safe_copy_to_user(u_stat, &stat, sizeof(stat));
}

#ifdef CONFIG_XENO_OPT_COBALT_EXTENSION

int cobalt_thread_extend(struct cobalt_extension *ext,
			 void *priv)
{
	struct cobalt_thread *thread = cobalt_current_thread();
	struct xnpersonality *prev;

	trace_cobalt_pthread_extend(thread->hkey.u_pth, ext->core.name);

	prev = xnshadow_push_personality(ext->core.muxid);
	if (prev == NULL)
		return -EINVAL;

	cobalt_set_extref(&thread->extref, ext, priv);
	XENO_BUGON(NUCLEUS, prev != &cobalt_personality);

	return 0;
}
EXPORT_SYMBOL_GPL(cobalt_thread_extend);

void cobalt_thread_restrict(void)
{
	struct cobalt_thread *thread = cobalt_current_thread();

	trace_cobalt_pthread_restrict(thread->hkey.u_pth,
		      xnthread_personality(&thread->threadbase)->name);
	xnshadow_pop_personality(&cobalt_personality);
	cobalt_set_extref(&thread->extref, NULL, NULL);
}
EXPORT_SYMBOL_GPL(cobalt_thread_restrict);

#endif /* !CONFIG_XENO_OPT_COBALT_EXTENSION */

int cobalt_sched_min_prio(int policy)
{
	int ret;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_SPORADIC:
	case SCHED_TP:
	case SCHED_QUOTA:
		ret = XNSCHED_FIFO_MIN_PRIO;
		break;
	case SCHED_COBALT:
		ret = XNSCHED_RT_MIN_PRIO;
		break;
	case SCHED_NORMAL:
	case SCHED_WEAK:
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	trace_cobalt_sched_min_prio(policy, ret);

	return ret;
}

int cobalt_sched_max_prio(int policy)
{
	int ret;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_SPORADIC:
	case SCHED_TP:
	case SCHED_QUOTA:
		ret = XNSCHED_FIFO_MAX_PRIO;
		break;
	case SCHED_COBALT:
		ret = XNSCHED_RT_MAX_PRIO;
		break;
	case SCHED_NORMAL:
		ret = 0;
		break;
	case SCHED_WEAK:
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
		ret = XNSCHED_FIFO_MAX_PRIO;
#else
		ret 0;
#endif
		break;
	default:
		ret = -EINVAL;
	}

	trace_cobalt_sched_max_prio(policy, ret);

	return ret;
}

int cobalt_sched_yield(void)
{
	struct cobalt_thread *curr = cobalt_current_thread();
	int ret = 0;

	trace_cobalt_pthread_yield(0);

	/* Maybe some extension wants to handle this. */
  	if (cobalt_call_extension(sched_yield, &curr->extref, ret) && ret)
		return ret > 0 ? 0 : ret;

	xnthread_resume(&curr->threadbase, 0);
	if (xnsched_run())
		return 0;

	/*
	 * If the round-robin move did not beget any context switch to
	 * a thread running in primary mode, then wait for the next
	 * linux context switch to happen.
	 *
	 * Rationale: it is most probably unexpected that
	 * sched_yield() does not cause any context switch, since this
	 * service is commonly used for implementing a poor man's
	 * cooperative scheduling. By waiting for a context switch to
	 * happen in the regular kernel, we guarantee that the CPU has
	 * been relinquished for a while.
	 *
	 * Typically, this behavior allows a thread running in primary
	 * mode to effectively yield the CPU to a thread of
	 * same/higher priority stuck in secondary mode.
	 *
	 * NOTE: calling xnshadow_yield() with no timeout
	 * (i.e. XN_INFINITE) is probably never a good idea. This
	 * means that a SCHED_FIFO non-rt thread stuck in a tight loop
	 * would prevent the caller from waking up, since no
	 * linux-originated schedule event would happen for unblocking
	 * it on the current CPU. For this reason, we pass the
	 * arbitrary TICK_NSEC value to limit the wait time to a
	 * reasonable amount.
	 */
	return xnshadow_yield(TICK_NSEC, TICK_NSEC);
}

#ifdef CONFIG_XENO_OPT_SCHED_TP

static inline
int set_tp_config(int cpu, union sched_config *config, size_t len)
{
	xnticks_t offset, duration, next_offset;
	struct xnsched_tp_schedule *gps, *ogps;
	struct xnsched_tp_window *w;
	struct sched_tp_window *p;
	struct xnsched *sched;
	spl_t s;
	int n;

	if (config->tp.nr_windows == 0) {
		gps = NULL;
		goto set_schedule;
	}

	gps = xnmalloc(sizeof(*gps) + config->tp.nr_windows * sizeof(*w));
	if (gps == NULL)
		goto fail;

	for (n = 0, p = config->tp.windows, w = gps->pwins, next_offset = 0;
	     n < config->tp.nr_windows; n++, p++, w++) {
		/*
		 * Time windows must be strictly contiguous. Holes may
		 * be defined using windows assigned to the pseudo
		 * partition #-1.
		 */
		offset = ts2ns(&p->offset);
		if (offset != next_offset)
			goto cleanup_and_fail;

		duration = ts2ns(&p->duration);
		if (duration <= 0)
			goto cleanup_and_fail;

		if (p->ptid < -1 ||
		    p->ptid >= CONFIG_XENO_OPT_SCHED_TP_NRPART)
			goto cleanup_and_fail;

		w->w_offset = next_offset;
		w->w_part = p->ptid;
		next_offset += duration;
	}

	atomic_set(&gps->refcount, 1);
	gps->pwin_nr = n;
	gps->tf_duration = next_offset;
set_schedule:
	sched = xnsched_struct(cpu);
	xnlock_get_irqsave(&nklock, s);
	ogps = xnsched_tp_set_schedule(sched, gps);
	xnsched_tp_start_schedule(sched);
	xnlock_put_irqrestore(&nklock, s);

	if (ogps)
		xnsched_tp_put_schedule(ogps);

	return 0;

cleanup_and_fail:
	xnfree(gps);
fail:
	return -EINVAL;
}

static inline
ssize_t get_tp_config(int cpu, union sched_config __user *u_config,
		      size_t len)
{
	struct xnsched_tp_window *pw, *w;
	struct xnsched_tp_schedule *gps;
	struct sched_tp_window *pp, *p;
	union sched_config *config;
	struct xnsched *sched;
	ssize_t ret = 0, elen;
	spl_t s;
	int n;

	xnlock_get_irqsave(&nklock, s);

	sched = xnsched_struct(cpu);
	gps = xnsched_tp_get_schedule(sched);
	if (gps == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return 0;
	}

	xnlock_put_irqrestore(&nklock, s);

	elen = sched_tp_confsz(gps->pwin_nr);
	if (elen > len) {
		ret = -ENOSPC;
		goto out;
	}

	config = xnmalloc(elen);
	if (config == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	config->tp.nr_windows = gps->pwin_nr;
	for (n = 0, pp = p = config->tp.windows, pw = w = gps->pwins;
	     n < gps->pwin_nr; pp = p, p++, pw = w, w++, n++) {
		ns2ts(&p->offset, w->w_offset);
		ns2ts(&pp->duration, w->w_offset - pw->w_offset);
		p->ptid = w->w_part;
	}
	ns2ts(&pp->duration, gps->tf_duration - pw->w_offset);
	ret = __xn_safe_copy_to_user(u_config, config, elen);
out:
	xnsched_tp_put_schedule(gps);

	return ret ?: elen;
}

#else /* !CONFIG_XENO_OPT_SCHED_TP */

static inline int
set_tp_config(int cpu, union sched_config *config, size_t len)
{
	return -EINVAL;
}

static inline ssize_t
get_tp_config(int cpu, union sched_config __user *u_config,
	      size_t len)
{
	return -EINVAL;
}

#endif /* !CONFIG_XENO_OPT_SCHED_TP */

#ifdef CONFIG_XENO_OPT_SCHED_QUOTA

static inline
int set_quota_config(int cpu, const union sched_config *config, size_t len)
{
	const struct __sched_config_quota *p = &config->quota;
	int ret = -ESRCH, quota_percent, quota_peak_percent;
	struct xnsched_quota_group *tg;
	struct xnsched *sched;
	spl_t s;

	if (len < sizeof(*p))
		return -EINVAL;

	if (p->op == sched_quota_add) {
		tg = xnmalloc(sizeof(*tg));
		if (tg == NULL)
			return -ENOMEM;
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		ret = xnsched_quota_create_group(tg, sched);
		xnlock_put_irqrestore(&nklock, s);
		if (ret == 0)
			ret = __xn_safe_copy_to_user(p->add.tgid_r, &tg->tgid,
						     sizeof(tg->tgid));
		if (ret)
			xnfree(tg);
		return ret;
	}

	if (p->op == sched_quota_remove) {
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		tg = xnsched_quota_find_group(sched, p->remove.tgid);
		if (tg) {
			ret = xnsched_quota_destroy_group(tg);
			xnlock_put_irqrestore(&nklock, s);
			if (ret == 0)
				xnfree(tg);
			return ret;
		}
		xnlock_put_irqrestore(&nklock, s);
		return ret;
	}

	if (p->op == sched_quota_set) {
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		tg = xnsched_quota_find_group(sched, p->set.tgid);
		if (tg) {
			xnsched_quota_set_limit(tg,
						p->set.quota,
						p->set.quota_peak);
			ret = 0;
		}
		xnlock_put_irqrestore(&nklock, s);
		return ret;
	}

	if (p->op == sched_quota_get) {
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		tg = xnsched_quota_find_group(sched, p->get.tgid);
		if (tg) {
			quota_percent = tg->quota_percent;
			quota_peak_percent = tg->quota_peak_percent;
			ret = 0;
		}
		xnlock_put_irqrestore(&nklock, s);
		if (ret)
			return ret;
		ret = __xn_safe_copy_to_user(p->get.quota_r, &quota_percent,
					     sizeof(quota_percent));
		if (ret)
			return ret;
		ret = __xn_safe_copy_to_user(p->get.quota_peak_r,
					     &quota_peak_percent,
					     sizeof(quota_peak_percent));
		return ret;
	}

	return -EINVAL;
}

static inline
ssize_t get_quota_config(int cpu, union sched_config __user *u_config,
			 size_t len)
{
	union sched_config buf;
	
	if (__xn_safe_copy_from_user(&buf, (const void __user *)u_config, len))
		return -EFAULT;

	buf.quota.op = sched_quota_get;

	return set_quota_config(cpu, &buf, len);
}

#else /* !CONFIG_XENO_OPT_SCHED_QUOTA */

static inline
int set_quota_config(int cpu, const union sched_config *config, size_t len)
{
	return -EINVAL;
}

static inline
ssize_t get_quota_config(int cpu, union sched_config __user *u_config,
			 size_t len)
{
	return -EINVAL;
}

#endif /* !CONFIG_XENO_OPT_SCHED_QUOTA */

/**
 * Load CPU-specific scheduler settings for a given policy.  A
 * configuration is strictly local to the target @a cpu, and may
 * differ from other processors.
 *
 * @param cpu processor to load the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, SCHED_TP and SCHED_QUOTA are valid.
 *
 * @param u_config a pointer to the configuration data to load on @a
 * cpu, applicable to @a policy.
 *
 * @par Settings applicable to SCHED_TP
 *
 * This call installs the temporal partitions for @a cpu.
 *
 * - config.tp.windows should be a non-null set of time windows,
 * defining the scheduling time slots for @a cpu. Each window defines
 * its offset from the start of the global time frame
 * (windows[].offset), a duration (windows[].duration), and the
 * partition id it applies to (windows[].ptid).
 *
 * Time windows must be strictly contiguous, i.e. windows[n].offset +
 * windows[n].duration shall equal windows[n + 1].offset.
 * If windows[].ptid is in the range
 * [0..CONFIG_XENO_OPT_SCHED_TP_NRPART-1], SCHED_TP threads which
 * belong to the partition being referred to may run for the duration
 * of the time window.
 *
 * Time holes may be defined using windows assigned to the pseudo
 * partition #-1, during which no SCHED_TP threads may be scheduled.
 *
 * - config.tp.nr_windows should define the number of elements present
 * in the config.tp.windows[] array.
 *
 * @par Settings applicable to SCHED_QUOTA
 *
 * This call manages thread groups running on @a cpu.
 *
 * - config.quota.op should define the operation to be carried
 * out. Valid operations are:
 *
 *    - sched_quota_add for creating a new thread group on @a cpu.
 *      The new group identifier will be written back to
 *      config.quota.add.tgid_r upon success. A new group is given no
 *      initial runtime budget when created. sched_quota_set should be
 *      issued to enable it.
 *
 *    - sched_quota_remove for deleting a thread group on @a cpu. The
 *      group identifier should be passed in config.quota.remove.tgid.
 *
 *    - sched_quota_set for updating the scheduling parameters of a
 *      thread group defined on @a cpu. The group identifier should be
 *      passed in config.quota.set.tgid, along with the allotted
 *      percentage of the quota interval (config.quota.set.quota), and
 *      the peak percentage allowed (config.quota.set.quota_peak).
 *
 *    - sched_quota_get for retrieving the scheduling parameters of a
 *      thread group defined on @a cpu. The group identifier should be
 *      passed in config.quota.get.tgid. The allotted percentage of
 *      the quota interval (config.quota.get.quota_r), and the peak
 *      percentage (config.quota.get.quota_peak_r) will be written to
 *      the given output variables. The result of this operation is
 *      identical to calling sched_getconfig_np().
 *
 * @param len overall length of the configuration data (in bytes).
 *
 * @return 0 on success;
 * @return an error number if:
 *
 * - EINVAL, @a cpu is invalid, or @a policy is unsupported by the
 * current kernel configuration, @a len is invalid, or @a u_config
 * contains invalid parameters.
 *
 * - ENOMEM, lack of memory to perform the operation.
 *
 * - EBUSY, with @a policy equal to SCHED_QUOTA, if an attempt is made
 *   to remove a thread group which still manages threads.
 *
 * - ESRCH, with @a policy equal to SCHED_QUOTA, if the group
 *   identifier required to perform the operation is not valid.
 */
int cobalt_sched_setconfig_np(int cpu, int policy,
			      const union sched_config __user *u_config,
			      size_t len)
{
	union sched_config *buf;
	int ret;

	trace_cobalt_sched_set_config(cpu, policy, len);

	if (cpu < 0 || cpu >= NR_CPUS || !cpu_online(cpu))
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	buf = xnmalloc(len);
	if (buf == NULL)
		return -ENOMEM;

	if (__xn_safe_copy_from_user(buf, (const void __user *)u_config, len)) {
		ret = -EFAULT;
		goto out;
	}

	switch (policy)	{
	case SCHED_TP:
		ret = set_tp_config(cpu, buf, len);
		break;
	case SCHED_QUOTA:
		ret = set_quota_config(cpu, buf, len);
		break;
	default:
		ret = -EINVAL;
	}
out:
	xnfree(buf);

	return ret;
}

/**
 * Retrieve CPU-specific scheduler settings for a given policy.  A
 * configuration is strictly local to the target @a cpu, and may
 * differ from other processors.
 *
 * @param cpu processor to retrieve the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, SCHED_TP and SCHED_QUOTA are valid.
 *
 * @param u_config a pointer to a memory area where the configuration
 * data will be copied back. This area must be at least @a len bytes
 * long.
 *
 * @param len overall length of the configuration data (in bytes).
 *
 * @return the number of bytes copied to @a u_config on success;
 * @return a negative error number if:
 *
 * - EINVAL, @a cpu is invalid, or @a policy is unsupported by the
 * current kernel configuration, or @a len cannot hold the retrieved
 * configuration data.
 *
 * - ESRCH, with @a policy equal to SCHED_QUOTA, if the group
 *   identifier required to perform the operation is not valid.
 *
 * - ENOMEM, lack of memory to perform the operation.
 *
 * - ENOSPC, @a len is too short.
 */
ssize_t cobalt_sched_getconfig_np(int cpu, int policy,
				  union sched_config __user *u_config,
				  size_t len)
{
	ssize_t ret;

	switch (policy)	{
	case SCHED_TP:
		ret = get_tp_config(cpu, u_config, len);
		break;
	case SCHED_QUOTA:
		ret = get_quota_config(cpu, u_config, len);
		break;
	default:
		ret = -EINVAL;
	}

	trace_cobalt_sched_get_config(cpu, policy, ret);

	return ret;
}

/*@}*/
