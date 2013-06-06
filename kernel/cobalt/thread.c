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
#include <linux/jhash.h>
#include "thread.h"
#include "cancel.h"
#include "timer.h"

xnticks_t cobalt_time_slice;

static const pthread_attr_t default_thread_attr = {
	.magic = COBALT_THREAD_ATTR_MAGIC,
	.detachstate = PTHREAD_CREATE_JOINABLE,
	.inheritsched = PTHREAD_EXPLICIT_SCHED,
	.policy = SCHED_OTHER,
	.schedparam_ex = {
		.sched_priority = 0
	},
	.name = NULL,
	.fp = 1,
	.affinity = XNPOD_ALL_CPUS,
};

static unsigned cobalt_get_magic(void)
{
	return COBALT_SKIN_MAGIC;
}

static struct xnthread_operations cobalt_thread_ops = {
	.get_magic = &cobalt_get_magic,
};

#define PTHREAD_HSLOTS (1 << 8)	/* Must be a power of 2 */

struct cobalt_hash {
	pthread_t pthread;
	pid_t pid;
	struct cobalt_hkey hkey;
	struct cobalt_hash *next;
};

struct pid_hash {
	pid_t pid;
	pthread_t pthread;
	struct pid_hash *next;
};

static struct cobalt_hash *pthread_table[PTHREAD_HSLOTS];

static struct pid_hash *pid_table[PTHREAD_HSLOTS];

static inline struct cobalt_hash *
cobalt_thread_hash(const struct cobalt_hkey *hkey, pthread_t pthread, pid_t pid)
{
	struct cobalt_hash **pthead, *ptslot;
	struct pid_hash **pidhead, *pidslot;
	u32 hash;
	void *p;
	spl_t s;

	p = xnmalloc(sizeof(*ptslot) + sizeof(*pidslot));
	if (p == NULL)
		return NULL;

	ptslot = p;
	ptslot->hkey = *hkey;
	ptslot->pthread = pthread;
	ptslot->pid = pid;
	hash = jhash2((u32 *)&ptslot->hkey,
		      sizeof(ptslot->hkey) / sizeof(u32), 0);
	pthead = &pthread_table[hash & (PTHREAD_HSLOTS - 1)];

	pidslot = p + sizeof(*ptslot);
	pidslot->pid = pid;
	pidslot->pthread = pthread;
	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);
	pidhead = &pid_table[hash & (PTHREAD_HSLOTS - 1)];

	xnlock_get_irqsave(&nklock, s);
	ptslot->next = *pthead;
	*pthead = ptslot;
	pidslot->next = *pidhead;
	*pidhead = pidslot;
	xnlock_put_irqrestore(&nklock, s);

	return ptslot;
}

static inline void cobalt_thread_unhash(const struct cobalt_hkey *hkey)
{
	struct cobalt_hash **pttail, *ptslot;
	struct pid_hash **pidtail, *pidslot;
	pid_t pid;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *) hkey, sizeof(*hkey) / sizeof(u32), 0);
	pttail = &pthread_table[hash & (PTHREAD_HSLOTS - 1)];

	xnlock_get_irqsave(&nklock, s);

	ptslot = *pttail;
	while (ptslot &&
	       (ptslot->hkey.u_tid != hkey->u_tid ||
		ptslot->hkey.mm != hkey->mm)) {
		pttail = &ptslot->next;
		ptslot = *pttail;
	}

	if (ptslot == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return;
	}

	*pttail = ptslot->next;
	pid = ptslot->pid;
	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);
	pidtail = &pid_table[hash & (PTHREAD_HSLOTS - 1)];
	pidslot = *pidtail;
	while (pidslot && pidslot->pid != pid) {
		pidtail = &pidslot->next;
		pidslot = *pidtail;
	}
	/* pidslot must be found here. */
	XENO_BUGON(POSIX, !(pidslot && pidtail));
	*pidtail = pidslot->next;

	xnlock_put_irqrestore(&nklock, s);

	xnfree(ptslot);
	xnfree(pidslot);
}

static pthread_t thread_find(const struct cobalt_hkey *hkey)
{
	struct cobalt_hash *ptslot;
	pthread_t pthread;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *) hkey, sizeof(*hkey) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	ptslot = pthread_table[hash & (PTHREAD_HSLOTS - 1)];

	while (ptslot != NULL &&
	       (ptslot->hkey.u_tid != hkey->u_tid || ptslot->hkey.mm != hkey->mm))
		ptslot = ptslot->next;

	pthread = ptslot ? ptslot->pthread : NULL;

	xnlock_put_irqrestore(&nklock, s);

	return pthread;
}

static void thread_destroy(pthread_t thread)
{
	removeq(thread->container, &thread->link);
	xnsynch_destroy(&thread->monitor_synch);
	xnheap_schedule_free(&kheap, thread, &thread->link);
}

static void thread_delete_hook(struct xnthread *thread)
{
	pthread_t tid = thread2pthread(thread);

	if (tid == NULL)
		return;

	cobalt_mark_deleted(tid);
	cobalt_timer_cleanup_thread(tid);
	thread_destroy(tid);

	cobalt_thread_unhash(&tid->hkey);
	if (xnthread_test_state(thread, XNMAPPED))
		xnshadow_unmap(thread);
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
 * Typically, SCHED_WEAK, SCHED_SPORADIC or SCHED_TP parameters can be
 * retrieved from this call.
 *
 * @param tid target thread;
 *
 * @param pol address where the scheduling policy of @a tid is stored on
 * success;
 *
 * @param par address where the scheduling parameters of @a tid are
 * stored on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 *
 */
static inline int
pthread_getschedparam_ex(pthread_t tid, int *pol, struct sched_param_ex *par)
{
	struct xnsched_class *base_class;
	struct xnthread *thread;
	int prio;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(tid, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	thread = &tid->threadbase;
	base_class = xnthread_base_class(thread);
	*pol = tid->sched_u_policy;
	prio = xnthread_base_priority(thread);
	par->sched_priority = prio;

	if (base_class == &xnsched_class_rt) {
		if (xnthread_test_state(thread, XNRRB))
			ns2ts(&par->sched_rr_quantum, xnthread_time_slice(thread));
		goto unlock_and_exit;
	}

#ifdef CONFIG_XENO_OPT_SCHED_WEAK
	if (base_class == &xnsched_class_weak) {
		if (*pol != SCHED_WEAK)
			par->sched_priority = -par->sched_priority;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	if (base_class == &xnsched_class_sporadic) {
		par->sched_ss_low_priority = thread->pss->param.low_prio;
		ns2ts(&par->sched_ss_repl_period, thread->pss->param.repl_period);
		ns2ts(&par->sched_ss_init_budget, thread->pss->param.init_budget);
		par->sched_ss_max_repl = thread->pss->param.max_repl;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	if (base_class == &xnsched_class_tp) {
		par->sched_tp_partition = thread->tps - thread->sched->tp.partitions;
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
 * @param tid address where the identifier of the new thread will be stored on
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
 *   and the calling thread does not belong to the POSIX skin;
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_create.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating or shadowing a Xenomai thread for the first time in
 * user-space, Xenomai installs a handler for the SIGWINCH signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGWINCH signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGWINCH after
 * creating or shadowing the first Xenomai thread, you have to
 * explicitly call the function xeno_sigwinch_handler at the beginning
 * of your signal handler, using its return to know if the signal was
 * in fact an internal signal of Xenomai (in which case it returns 1),
 * or if you should handle the signal (in which case it returns
 * 0). xeno_sigwinch_handler prototype is:
 *
 * <b>int xeno_sigwinch_handler(int sig, siginfo_t *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction,
 * using the SA_SIGINFO flag, and pass all the arguments you received
 * to xeno_sigwinch_handler.
 */
static inline int pthread_create(pthread_t *tid, const pthread_attr_t *attr)
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	struct xnthread_init_attr iattr;
	pthread_t thread, cur;
	xnflags_t flags = 0;
	const char *name;
	int prio, ret, pol;
	spl_t s;

	if (attr && attr->magic != COBALT_THREAD_ATTR_MAGIC)
		return -EINVAL;

	thread = (pthread_t)xnmalloc(sizeof(*thread));
	if (thread == NULL)
		return -EAGAIN;

	cur = cobalt_current_thread();
	thread->attr = attr ? *attr : default_thread_attr;
	if (thread->attr.inheritsched == PTHREAD_INHERIT_SCHED) {
		/*
		 * cur may be NULL if pthread_create is not called by
		 * a cobalt thread, in which case trying to inherit
		 * scheduling parameters is treated as an error.
		 */
		if (cur == NULL) {
			xnfree(thread);
			return -EINVAL;
		}

		pthread_getschedparam_ex(cur, &thread->attr.policy,
					 &thread->attr.schedparam_ex);
	}

	/*
	 * NOTE: The user-defined policy may be different than ours,
	 * e.g. SCHED_FIFO,prio=-7 from userland would be interpreted
	 * as SCHED_WEAK,prio=7 in kernel space.
	 */
	pol = thread->attr.policy;
	prio = thread->attr.schedparam_ex.sched_priority;
	if (prio < 0) {
		prio = -prio;
		pol = SCHED_WEAK;
	}
	name = thread->attr.name;
	flags |= XNUSER;

	if (thread->attr.fp)
		flags |= XNFPU;

	iattr.name = name;
	iattr.flags = flags;
	iattr.ops = &cobalt_thread_ops;

	/*
	 * When the weak scheduling class is compiled in, SCHED_WEAK
	 * and SCHED_OTHER threads are scheduled by
	 * xnsched_class_weak, at their respective priority
	 * levels. Otherwise, SCHED_OTHER is scheduled by
	 * xnsched_class_rt at priority level #0.
	 */
	switch (pol) {
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
	case SCHED_OTHER:
	case SCHED_WEAK:
		param.weak.prio = prio;
		sched_class = &xnsched_class_weak;
		break;
#endif
	default:
		param.rt.prio = prio;
		sched_class = &xnsched_class_rt;
		break;
	}

	if (xnpod_init_thread(&thread->threadbase,
			      &iattr, sched_class, &param) != 0) {
		xnfree(thread);
		return -EAGAIN;
	}

	thread->attr.name = xnthread_name(&thread->threadbase);

	inith(&thread->link);

	thread->magic = COBALT_THREAD_MAGIC;
	xnsynch_init(&thread->monitor_synch, XNSYNCH_FIFO, NULL);
	inith(&thread->monitor_link);
	thread->monitor_queued = 0;
	thread->sched_u_policy = thread->attr.policy;

	cobalt_timer_init_thread(thread);

	if (thread->attr.policy == SCHED_RR)
		xnpod_set_thread_tslice(&thread->threadbase, cobalt_time_slice);

	xnlock_get_irqsave(&nklock, s);
	thread->container = &cobalt_kqueues(0)->threadq;
	appendq(thread->container, &thread->link);
	xnlock_put_irqrestore(&nklock, s);

	thread->hkey.u_tid = 0;
	thread->hkey.mm = NULL;

	/*
	 * We need an anonymous registry entry to obtain a handle for
	 * fast mutex locking.
	 */
	ret = xnthread_register(&thread->threadbase, "");
	if (ret) {
		thread_destroy(thread);
		return ret;
	}

	*tid = thread; /* Must be done before the thread is started. */

	return 0;
}

/**
 * Make a thread periodic.
 *
 * This service make the POSIX skin thread @a thread periodic.
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
 * - ENOTSUP, the specified clock is unsupported;
 *
 * Rescheduling: always, until the @a starttp start time has been reached.
 */
static inline int pthread_make_periodic_np(pthread_t thread,
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
		return ENOTSUP;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		ret = -ESRCH;
		goto unlock_and_exit;
	}

	start = ts2ns(starttp);
	period = ts2ns(periodtp);
	ret = xnpod_set_thread_periodic(&thread->threadbase, start,
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
 *
 * PTHREAD_LOCK_SCHED is valid for any Xenomai thread, the other bits are only
 * valid for Xenomai user-space threads.
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
	const xnflags_t valid_flags = XNLOCK|XNTRAPSW;
	xnthread_t *cur = xnpod_current_thread();
	xnflags_t old;

	/*
	 * The conforming mode bit is actually zero, since jumping to
	 * this code entailed switching to the proper mode already.
	 */
	if ((clrmask & ~valid_flags) != 0 || (setmask & ~valid_flags) != 0)
		return -EINVAL;

	old = xnpod_set_thread_mode(cur, clrmask, setmask);
	if (mode_r)
		*mode_r = old;

	if ((clrmask & ~setmask) & XNLOCK)
		/* Reschedule if the scheduler has been unlocked. */
		xnpod_schedule();

	return 0;
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
 * Typically, a Xenomai thread policy can be set to SCHED_WEAK,
 * SCHED_SPORADIC or SCHED_TP using this call.
 *
 * This service set the scheduling policy of the Xenomai thread @a tid
 * to the value @a u_pol, and its scheduling parameters (e.g. its
 * priority) to the value pointed to by @a par.
 *
 * If @a tid does not match the identifier of a Xenomai thread, this
 * action falls back to the regular pthread_setschedparam() service.
 *
 * @param tid target thread;
 *
 * @param u_pol scheduling policy, one of SCHED_WEAK, SCHED_FIFO,
 * SCHED_COBALT, SCHED_RR, SCHED_SPORADIC, SCHED_TP or SCHED_OTHER;
 *
 * @param par scheduling parameters address. As a special exception, a
 * negative sched_priority value is interpreted as if SCHED_WEAK was
 * given in @a u_pol, using the absolute value of this parameter as
 * the weak priority level.
 *
 * When CONFIG_XENO_OPT_SCHED_WEAK is enabled, SCHED_WEAK exhibits
 * priority levels in the [0..99] range (inclusive). Otherwise,
 * sched_priority must be zero for the SCHED_WEAK policy.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid;
 * - EINVAL, @a u_pol or @a par->sched_priority is invalid;
 * - EAGAIN, in user-space, insufficient memory exists in the system heap,
 *   increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EFAULT, in user-space, @a par is an invalid address;
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
 * user-space, Xenomai installs a handler for the SIGWINCH signal. If you had
 * installed a handler before that, it will be automatically called by Xenomai
 * for SIGWINCH signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGWINCH after creating
 * or shadowing the first Xenomai thread, you have to explicitly call the
 * function xeno_sigwinch_handler at the beginning of your signal handler,
 * using its return to know if the signal was in fact an internal signal of
 * Xenomai (in which case it returns 1), or if you should handle the signal (in
 * which case it returns 0). xeno_sigwinch_handler prototype is:
 *
 * <b>int xeno_sigwinch_handler(int sig, siginfo_t *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction, using the
 * SA_SIGINFO flag, and pass all the arguments you received to
 * xeno_sigwinch_handler.
 *
 * pthread_setschedparam_ex() may switch the caller to secondary mode.
 */
static inline int
pthread_setschedparam_ex(pthread_t tid, int u_pol, const struct sched_param_ex *par)
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	struct xnthread *thread;
	xnticks_t tslice;
	int prio, pol;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(tid, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	thread = &tid->threadbase;
	prio = par->sched_priority;
	tslice = XN_INFINITE;
	pol = u_pol;

	if (prio < 0) {
		prio = -prio;
		pol = SCHED_WEAK;
	}
	sched_class = &xnsched_class_rt;
	param.rt.prio = prio;

	switch (pol) {
	case SCHED_OTHER:
		if (prio)
			goto fail;
		/* falldown wanted */
	case SCHED_WEAK:
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
		if (prio < XNSCHED_WEAK_MIN_PRIO || prio > XNSCHED_WEAK_MAX_PRIO)
			goto fail;
		param.weak.prio = prio;
		sched_class = &xnsched_class_weak;
#else
		if (prio)
			goto fail;
#endif
		break;
	case SCHED_RR:
		tslice = ts2ns(&par->sched_rr_quantum);
		if (tslice == XN_INFINITE)
			tslice = xnthread_time_slice(thread);
		/* falldown wanted */
	case SCHED_FIFO:
		if (prio < XNSCHED_FIFO_MIN_PRIO || prio > XNSCHED_FIFO_MAX_PRIO)
			goto fail;
		break;
	case SCHED_COBALT:
		if (prio < XNSCHED_RT_MIN_PRIO || prio > XNSCHED_RT_MAX_PRIO)
			goto fail;
		break;
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	case SCHED_SPORADIC:
		param.pss.normal_prio = par->sched_priority;
		param.pss.low_prio = par->sched_ss_low_priority;
		param.pss.current_prio = param.pss.normal_prio;
		param.pss.init_budget = ts2ns(&par->sched_ss_init_budget);
		param.pss.repl_period = ts2ns(&par->sched_ss_repl_period);
		param.pss.max_repl = par->sched_ss_max_repl;
		sched_class = &xnsched_class_sporadic;
		break;
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	case SCHED_TP:
		param.tp.prio = par->sched_priority;
		param.tp.ptid = par->sched_tp_partition;
		sched_class = &xnsched_class_tp;
		break;
#endif
	default:
	fail:
		xnlock_put_irqrestore(&nklock, s);
		return -EINVAL;
	}

	xnpod_set_thread_tslice(thread, tslice);
	tid->sched_u_policy = u_pol;
	xnpod_set_thread_schedparam(thread, sched_class, &param);

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/*
 * NOTE: there is no cobalt_thread_setschedparam syscall defined by
 * the Cobalt ABI. Useland changes scheduling parameters only via the
 * extended cobalt_thread_setschedparam_ex syscall.
 */
int cobalt_thread_setschedparam_ex(unsigned long tid,
				   int policy,
				   struct sched_param_ex __user *u_param,
				   unsigned long __user *u_window_offset,
				   int __user *u_promoted)
{
	struct sched_param_ex param;
	struct cobalt_hkey hkey;
	int ret, promoted = 0;
	pthread_t pthread;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	pthread = thread_find(&hkey);

	if (pthread == NULL && u_window_offset) {
		pthread = cobalt_thread_shadow(current, &hkey, u_window_offset);
		if (IS_ERR(pthread))
			return PTR_ERR(pthread);

		promoted = 1;
	}
	if (pthread)
		ret = pthread_setschedparam_ex(pthread, policy, &param);
	else
		ret = -EPERM;

	if (ret == 0 &&
	    __xn_safe_copy_to_user(u_promoted, &promoted, sizeof(promoted)))
		ret = -EFAULT;

	return ret;
}

/*
 * We want to keep the native pthread_t token unmodified for Xenomai
 * mapped threads, and keep it pointing at a genuine NPTL/LinuxThreads
 * descriptor, so that portions of the POSIX interface which are not
 * overriden by Xenomai fall back to the original Linux services.
 *
 * If the latter invoke Linux system calls, the associated shadow
 * thread will simply switch to secondary exec mode to perform
 * them. For this reason, we need an external index to map regular
 * pthread_t values to Xenomai's internal thread ids used in
 * syscalling the POSIX skin, so that the outer interface can keep on
 * using the former transparently.
 *
 * Semaphores and mutexes do not have this constraint, since we fully
 * override their respective interfaces with Xenomai-based
 * replacements.
 */

int cobalt_thread_create(unsigned long tid, int policy,
			 struct sched_param_ex __user *u_param,
			 unsigned long __user *u_window_offset)
{
	struct task_struct *p = current;
	struct sched_param_ex param;
	struct cobalt_hkey hkey;
	pthread_t pthread = NULL;
	pthread_attr_t attr;
	pid_t pid;
	int ret;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;
	/*
	 * We have been passed the pthread_t identifier the user-space
	 * POSIX library has assigned to our caller; we'll index our
	 * internal pthread_t descriptor in kernel space on it.
	 */
	hkey.u_tid = tid;
	hkey.mm = p->mm;

	/*
	 * Build a default thread attribute, then make sure that a few
	 * critical fields are set in a compatible fashion wrt to the
	 * calling context.
	 */
	attr = default_thread_attr;
	attr.policy = policy;
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.schedparam_ex = param;
	attr.fp = 1;
	attr.name = p->comm;

	ret = pthread_create(&pthread, &attr);
	if (ret)
		return ret;

	pid = task_pid_vnr(p);
	ret = xnshadow_map_user(&pthread->threadbase, u_window_offset);
	if (ret)
		goto fail;

	if (!cobalt_thread_hash(&hkey, pthread, pid)) {
		ret = -ENOMEM;
		goto fail;
	}

	pthread->hkey = hkey;

	return 0;

fail:
	xnpod_cancel_thread(&pthread->threadbase);

	return ret;
}

pthread_t cobalt_thread_shadow(struct task_struct *p,
			       struct cobalt_hkey *hkey,
			       unsigned long __user *u_window_offset)
{
	pthread_t pthread = NULL;
	pthread_attr_t attr;
	pid_t pid;
	int ret;

	attr = default_thread_attr;
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.name = p->comm;

	ret = pthread_create(&pthread, &attr);
	if (ret)
		return ERR_PTR(-ret);

	pid = task_pid_vnr(p);
	ret = xnshadow_map_user(&pthread->threadbase, u_window_offset);
	/*
	 * From now on, we run in primary mode, so we refrain from
	 * calling regular kernel services (e.g. like
	 * task_pid_vnr()).
	 */
	if (ret == 0 && !cobalt_thread_hash(hkey, pthread, pid))
		ret = -EAGAIN;

	if (ret)
		xnpod_cancel_thread(&pthread->threadbase);
	else
		pthread->hkey = *hkey;

	return ret ? ERR_PTR(ret) : pthread;
}

int cobalt_thread_make_periodic_np(unsigned long tid,
				   clockid_t clk_id,
				   struct timespec __user *u_startt,
				   struct timespec __user *u_periodt)
{
	struct timespec startt, periodt;
	struct cobalt_hkey hkey;
	pthread_t pthread;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	pthread = thread_find(&hkey);

	if (__xn_safe_copy_from_user(&startt, u_startt, sizeof(startt)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&periodt, u_periodt, sizeof(periodt)))
		return -EFAULT;

	return pthread_make_periodic_np(pthread, clk_id, &startt, &periodt);
}

int cobalt_thread_wait_np(unsigned long __user *u_overruns)
{
	unsigned long overruns;
	int ret;

	ret = xnpod_wait_thread_period(&overruns);

	if (u_overruns && (ret == 0 || ret == -ETIMEDOUT))
		__xn_put_user(overruns, u_overruns);

	return ret;
}

int cobalt_thread_set_mode_np(int clrmask, int setmask, int __user *u_mode_r)
{
	int ret, old;

	ret = pthread_set_mode_np(clrmask, setmask, &old);
	if (ret)
		return ret;

	if (u_mode_r && __xn_safe_copy_to_user(u_mode_r, &old, sizeof(old)))
		return -EFAULT;

	return 0;
}

int cobalt_thread_set_name_np(unsigned long tid, const char __user *u_name)
{
	char name[XNOBJECT_NAME_LEN];
	struct cobalt_hkey hkey;
	struct task_struct *p;
	pthread_t pthread;
	spl_t s;

	if (__xn_safe_strncpy_from_user(name, u_name,
					sizeof(name) - 1) < 0)
		return -EFAULT;

	name[sizeof(name) - 1] = '\0';
	hkey.u_tid = tid;
	hkey.mm = current->mm;

	xnlock_get_irqsave(&nklock, s);
	pthread = thread_find(&hkey);
	if (pthread == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	p = xnthread_host_task(&pthread->threadbase);
	get_task_struct(p);
	xnlock_put_irqrestore(&nklock, s);
	strncpy(p->comm, name, sizeof(p->comm));
	p->comm[sizeof(p->comm) - 1] = '\0';
	snprintf(xnthread_name(&pthread->threadbase),
		 XNOBJECT_NAME_LEN - 1, "%s", name);
	put_task_struct(p);

	return 0;
}

int cobalt_thread_probe_np(pid_t pid)
{
	struct pid_hash *pidslot;
	u32 hash;
	int ret;
	spl_t s;

	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	pidslot = pid_table[hash & (PTHREAD_HSLOTS - 1)];
	while (pidslot && pidslot->pid != pid)
		pidslot = pidslot->next;

	ret = pidslot ? 0 : -ESRCH;

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_thread_kill(unsigned long tid, int sig)
{
	struct cobalt_hkey hkey;
	pthread_t pthread;
	int ret;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	pthread = thread_find(&hkey);
	if (pthread == NULL)
		return -ESRCH;

	if (sig == 0)	/* Check for existence only. */
		return 0;

	/*
	 * We have to take care of self-suspension, when the
	 * underlying shadow thread is currently relaxed. In that
	 * case, we must switch back to primary before issuing the
	 * suspend call to the nucleus in pthread_kill(). Marking the
	 * cobalt_thread_kill syscall as __xn_exec_primary would be
	 * overkill, since no other signal would require this, so we
	 * handle that case locally here.
	 */
	if (sig == SIGSUSP && xnshadow_current_p(&pthread->threadbase)) {
		if (xnpod_root_p()) {
			ret = xnshadow_harden();
			if (ret)
				return ret;
		}
	}

	switch(sig) {
	/*
	 * Undocumented pseudo-signals to suspend/resume/unblock
	 * threads, force them out of primary mode or even demote them
	 * to the weak scheduling class/priority. Process them early,
	 * before anyone can notice...
	 */
	case SIGSUSP:
		/*
		 * The self-suspension case for shadows was handled at
		 * call site: we must be in primary mode already.
		 */
		xnpod_suspend_thread(&pthread->threadbase, XNSUSP,
				     XN_INFINITE, XN_RELATIVE, NULL);
		if (&pthread->threadbase == xnpod_current_thread() &&
		    xnthread_test_info(&pthread->threadbase, XNBREAK))
			ret = EINTR;
		break;

	case SIGRESM:
		xnpod_resume_thread(&pthread->threadbase, XNSUSP);
		goto resched;

	case SIGRELS:
		xnpod_unblock_thread(&pthread->threadbase);
		goto resched;

	case SIGKICK:
		xnshadow_kick(&pthread->threadbase);
		goto resched;

	case SIGDEMT:
		xnshadow_demote(&pthread->threadbase);
	  resched:
		xnpod_schedule();
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int cobalt_thread_stat(pid_t pid,
		       struct cobalt_threadstat __user *u_stat)
{
	struct cobalt_threadstat stat;
	struct pid_hash *pidslot;
	struct xnthread *thread;
	xnticks_t xtime;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	pidslot = pid_table[hash & (PTHREAD_HSLOTS - 1)];
	while (pidslot && pidslot->pid != pid)
		pidslot = pidslot->next;

	if (pidslot == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	thread = &pidslot->pthread->threadbase;
	stat.cpu = xnsched_cpu(thread->sched);
	xtime = xnthread_get_exectime(thread);
	if (xnthread_sched(thread)->curr == thread)
		xtime += xnstat_exectime_now() - xnthread_get_lastswitch(thread);
	stat.xtime = xnarch_tsc_to_ns(xtime);
	stat.msw = xnstat_counter_get(&thread->stat.ssw);
	stat.csw = xnstat_counter_get(&thread->stat.csw);
	stat.xsc = xnstat_counter_get(&thread->stat.xsc);
	stat.pf = xnstat_counter_get(&thread->stat.pf);
	stat.status = xnthread_state_flags(thread);
	stat.timeout = xnthread_get_timeout(thread, xnclock_read_monotonic());

	xnlock_put_irqrestore(&nklock, s);

	return __xn_safe_copy_to_user(u_stat, &stat, sizeof(stat));
}

/*
 * NOTE: there is no cobalt_thread_getschedparam syscall defined by
 * the Cobalt ABI. Useland retrieves scheduling parameters only via
 * the extended cobalt_thread_getschedparam_ex syscall.
 */
int cobalt_thread_getschedparam_ex(unsigned long tid,
				   int __user *u_policy,
				   struct sched_param_ex __user *u_param)
{
	struct sched_param_ex param;
	struct cobalt_hkey hkey;
	pthread_t pthread;
	int policy, ret;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	pthread = thread_find(&hkey);
	if (pthread == NULL)
		return -ESRCH;

	ret = pthread_getschedparam_ex(pthread, &policy, &param);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_policy, &policy, sizeof(int)))
		return -EFAULT;

	return __xn_safe_copy_to_user(u_param, &param, sizeof(param));
}

int cobalt_sched_min_prio(int policy)
{
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_SPORADIC:
	case SCHED_TP:
		return XNSCHED_FIFO_MIN_PRIO;
	case SCHED_COBALT:
		return XNSCHED_RT_MIN_PRIO;
	case SCHED_OTHER:
	case SCHED_WEAK:
		return 0;
	default:
		return -EINVAL;
	}
}

int cobalt_sched_max_prio(int policy)
{
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_SPORADIC:
	case SCHED_TP:
		return XNSCHED_FIFO_MAX_PRIO;
	case SCHED_COBALT:
		return XNSCHED_RT_MAX_PRIO;
	case SCHED_OTHER:
		return 0;
	case SCHED_WEAK:
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
		return XNSCHED_FIFO_MAX_PRIO;
#else
		return 0;
#endif
	default:
		return -EINVAL;
	}
}

int cobalt_sched_yield(void)
{
	pthread_t thread = thread2pthread(xnshadow_current());
	struct sched_param_ex param;
	int policy = SCHED_OTHER;

	pthread_getschedparam_ex(thread, &policy, &param);
	xnpod_yield();

	return policy == SCHED_OTHER;
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

	gps->pwin_nr = n;
	gps->tf_duration = next_offset;
	sched = xnpod_sched_slot(cpu);

	xnlock_get_irqsave(&nklock, s);
	ogps = xnsched_tp_set_schedule(sched, gps);
	xnsched_tp_start_schedule(sched);
	xnlock_put_irqrestore(&nklock, s);

	if (ogps)
		xnfree(ogps);

	return 0;

cleanup_and_fail:
	xnfree(gps);
fail:
	return -EINVAL;
}

#else /* !CONFIG_XENO_OPT_SCHED_TP */

static inline
int set_tp_config(int cpu, union sched_config *config, size_t len)
{
	return -EINVAL;
}

#endif /* !CONFIG_XENO_OPT_SCHED_TP */

/**
 * Load CPU-specific scheduler settings for a given policy.
 *
 * Currently, this call only supports the SCHED_TP policy, for loading
 * the temporal partitions. A configuration is strictly local to the
 * target @a cpu, and may differ from other processors.
 *
 * @param cpu processor to load the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, only SCHED_TP is valid.
 *
 * @param p a pointer to the configuration data to load for @a
 * cpu, applicable to @a policy.
 *
 * Settings applicable to SCHED_TP:
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
 * @param len size of the configuration data (in bytes).
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a cpu is invalid, @a policy is different from SCHED_TP,
 * SCHED_TP support is not compiled in (see CONFIG_XENO_OPT_SCHED_TP),
 * @a len is zero, or @a p contains invalid parameters.
 * - ENOMEM, lack of memory to perform the operation.
 */
int cobalt_sched_setconfig_np(int cpu, int policy,
			      union sched_config __user *u_config, size_t len)
{
	union sched_config *buf;
	int ret;

	if (cpu < 0 || cpu >= NR_CPUS || !cpu_online(cpu))
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	buf = xnmalloc(len);
	if (buf == NULL)
		return -ENOMEM;

	if (__xn_safe_copy_from_user(buf, (void __user *)u_config, len)) {
		ret = -EFAULT;
		goto out;
	}

	switch (policy)	{
	case SCHED_TP:
		ret = set_tp_config(cpu, buf, len);
		break;
	default:
		ret = -EINVAL;
	}
out:
	xnfree(buf);

	return ret;
}

void cobalt_thread_pkg_init(u_long rrperiod)
{
	initq(&cobalt_global_kqueues.threadq);
	cobalt_time_slice = rrperiod;
	xnpod_add_hook(XNHOOK_THREAD_DELETE, thread_delete_hook);
}

/*@}*/
