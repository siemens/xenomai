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
#include <linux/err.h>
#include <cobalt/uapi/signal.h>
#include "internal.h"
#include "thread.h"
#include "signal.h"
#include "timer.h"
#include "clock.h"

xnticks_t cobalt_time_slice;

static const pthread_attr_t default_thread_attr = {
	.magic = COBALT_THREAD_ATTR_MAGIC,
	.detachstate = PTHREAD_CREATE_JOINABLE,
	.inheritsched = PTHREAD_EXPLICIT_SCHED,
	.policy = SCHED_NORMAL,
	.schedparam_ex = {
		.sched_priority = 0
	},
	.name = NULL,
	.affinity = XNPOD_ALL_CPUS,
};

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
thread_find_local(const struct cobalt_local_hkey *hkey)
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

static void thread_destroy(struct cobalt_thread *thread)
{
	list_del(&thread->link);
	xnsynch_destroy(&thread->monitor_synch);
	xnsynch_destroy(&thread->sigwait);
	xnheap_schedule_free(&kheap, thread, &thread->link);
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

struct xnpersonality *cobalt_thread_exit(struct xnthread *curr)
{
	struct cobalt_thread *thread;

	thread = container_of(curr, struct cobalt_thread, threadbase);
	/*
	 * Unhash first, to prevent further access to the TCB from
	 * userland.
	 */
	thread_unhash(&thread->hkey);
	cobalt_mark_deleted(thread);
	cobalt_signal_flush(thread);
	cobalt_timer_flush(thread);

	/* We don't stack over any personality, no chaining. */
	return NULL;
}

struct xnpersonality *cobalt_thread_unmap(struct xnthread *zombie) /* nklocked, IRQs off */
{
	struct cobalt_thread *thread;

	thread = container_of(zombie, struct cobalt_thread, threadbase);
	thread_destroy(thread);

	return NULL;
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
 * @param thread target thread;
 *
 * @param pol address where the scheduling policy of @a thread is stored on
 * success;
 *
 * @param par address where the scheduling parameters of @a thread are
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
pthread_getschedparam_ex(struct cobalt_thread *thread, int *pol, struct sched_param_ex *par)
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
	*pol = thread->sched_u_policy;
	prio = xnthread_base_priority(base_thread);
	par->sched_priority = prio;

	if (base_class == &xnsched_class_rt) {
		if (xnthread_test_state(base_thread, XNRRB))
			ns2ts(&par->sched_rr_quantum, xnthread_time_slice(base_thread));
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
		par->sched_ss_low_priority = base_thread->pss->param.low_prio;
		ns2ts(&par->sched_ss_repl_period, base_thread->pss->param.repl_period);
		ns2ts(&par->sched_ss_init_budget, base_thread->pss->param.init_budget);
		par->sched_ss_max_repl = base_thread->pss->param.max_repl;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	if (base_class == &xnsched_class_tp) {
		par->sched_tp_partition =
			base_thread->tps - base_thread->sched->tp.partitions;
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
 * user-space, Xenomai installs a handler for the SIGWINCH signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGWINCH signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGWINCH after
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
static inline int pthread_create(struct cobalt_thread **thread_p, const pthread_attr_t *attr)
{
	struct cobalt_thread *thread, *curr;
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	struct xnthread_init_attr iattr;
	int prio, ret, pol, n;
	spl_t s;

	if (attr && attr->magic != COBALT_THREAD_ATTR_MAGIC)
		return -EINVAL;

	thread = xnmalloc(sizeof(*thread));
	if (thread == NULL)
		return -EAGAIN;

	curr = cobalt_current_thread();
	thread->attr = attr ? *attr : default_thread_attr;
	if (thread->attr.inheritsched == PTHREAD_INHERIT_SCHED) {
		/*
		 * curr may be NULL if pthread_create is not called by
		 * a cobalt thread, in which case trying to inherit
		 * scheduling parameters is treated as an error.
		 */
		if (curr == NULL) {
			xnfree(thread);
			return -EINVAL;
		}

		pthread_getschedparam_ex(curr, &thread->attr.policy,
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

	iattr.name = thread->attr.name;
	iattr.flags = XNUSER|XNFPU;
	iattr.personality = &cobalt_personality;

	/*
	 * When the weak scheduling class is compiled in, SCHED_WEAK
	 * and SCHED_NORMAL threads are scheduled by
	 * xnsched_class_weak, at their respective priority
	 * levels. Otherwise, SCHED_NORMAL is scheduled by
	 * xnsched_class_rt at priority level #0.
	 */
	switch (pol) {
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
	case SCHED_NORMAL:
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
	thread->magic = COBALT_THREAD_MAGIC;
	xnsynch_init(&thread->monitor_synch, XNSYNCH_FIFO, NULL);
	thread->monitor_queued = 0;
	thread->sched_u_policy = thread->attr.policy;

	xnsynch_init(&thread->sigwait, XNSYNCH_FIFO, NULL);
	sigemptyset(&thread->sigpending);
	for (n = 0; n < _NSIG; n++)
		INIT_LIST_HEAD(thread->sigqueues + n);

	INIT_LIST_HEAD(&thread->timersq);

	cobalt_set_extref(&thread->extref, NULL, NULL);

	if (thread->attr.policy == SCHED_RR)
		xnpod_set_thread_tslice(&thread->threadbase, cobalt_time_slice);

	xnlock_get_irqsave(&nklock, s);
	thread->container = &cobalt_kqueues(0)->threadq;
	list_add_tail(&thread->link, thread->container);
	xnlock_put_irqrestore(&nklock, s);

	thread->hkey.u_pth = 0;
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

	*thread_p = thread; /* Must be done before the thread is started. */

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
	struct xnthread *cur = xnpod_current_thread();
	const int valid_flags = XNLOCK|XNTRAPSW;
	int old;

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
 * This service set the scheduling policy of the Xenomai thread @a thread
 * to the value @a u_pol, and its scheduling parameters (e.g. its
 * priority) to the value pointed to by @a par.
 *
 * If @a thread does not match the identifier of a Xenomai thread, this
 * action falls back to the regular pthread_setschedparam() service.
 *
 * @param thread target thread;
 *
 * @param u_pol scheduling policy, one of SCHED_WEAK, SCHED_FIFO,
 * SCHED_COBALT, SCHED_RR, SCHED_SPORADIC, SCHED_TP or SCHED_NORMAL;
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
 * - ESRCH, @a thread is invalid;
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
 * user-space, Xenomai installs a handler for the SIGWINCH signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGWINCH signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGWINCH after
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
pthread_setschedparam_ex(struct cobalt_thread *thread, int u_pol, const struct sched_param_ex *par)
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	struct xnthread *base_thread;
	xnticks_t tslice;
	int prio, pol;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC,
			       struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	base_thread = &thread->threadbase;
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
	case SCHED_NORMAL:
		if (prio)
			goto fail;
		/* falldown wanted */
	case SCHED_WEAK:
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
		if (prio < XNSCHED_WEAK_MIN_PRIO ||
		    prio > XNSCHED_WEAK_MAX_PRIO)
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
			tslice = xnthread_time_slice(base_thread);
		/* falldown wanted */
	case SCHED_FIFO:
		if (prio < XNSCHED_FIFO_MIN_PRIO ||
		    prio > XNSCHED_FIFO_MAX_PRIO)
			goto fail;
		break;
	case SCHED_COBALT:
		if (prio < XNSCHED_RT_MIN_PRIO ||
		    prio > XNSCHED_RT_MAX_PRIO)
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

	xnpod_set_thread_tslice(base_thread, tslice);
	thread->sched_u_policy = u_pol;
	xnpod_set_thread_schedparam(base_thread, sched_class, &param);

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

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
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	struct sched_param_ex param;
	int ret, promoted = 0;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_find_local(&hkey);

	if (thread == NULL && u_window_offset) {
		thread = cobalt_thread_shadow(current, &hkey, u_window_offset);
		if (IS_ERR(thread))
			return PTR_ERR(thread);

		promoted = 1;
	}

	if (thread)
		ret = pthread_setschedparam_ex(thread, policy, &param);
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
			 unsigned long __user *u_window_offset)
{
	struct cobalt_thread *thread = NULL;
	struct task_struct *p = current;
	struct cobalt_local_hkey hkey;
	struct sched_param_ex param;
	pthread_attr_t attr;
	pid_t pid;
	int ret;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;
	/*
	 * We have been passed the pthread_t identifier the user-space
	 * Cobalt library has assigned to our caller; we'll index our
	 * internal pthread_t descriptor in kernel space on it.
	 */
	hkey.u_pth = pth;
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
	attr.name = p->comm;

	ret = pthread_create(&thread, &attr);
	if (ret)
		return ret;

	pid = task_pid_vnr(p);
	ret = xnshadow_map_user(&thread->threadbase, u_window_offset);
	if (ret)
		goto fail;

	if (!thread_hash(&hkey, thread, pid)) {
		ret = -ENOMEM;
		goto fail;
	}

	thread->hkey = hkey;

	return 0;

fail:
	xnpod_cancel_thread(&thread->threadbase);

	return ret;
}

struct cobalt_thread *cobalt_thread_shadow(struct task_struct *p,
			       struct cobalt_local_hkey *hkey,
			       unsigned long __user *u_window_offset)
{
	struct cobalt_thread *thread = NULL;
	pthread_attr_t attr;
	pid_t pid;
	int ret;

	attr = default_thread_attr;
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.name = p->comm;

	ret = pthread_create(&thread, &attr);
	if (ret)
		return ERR_PTR(-ret);

	pid = task_pid_vnr(p);
	ret = xnshadow_map_user(&thread->threadbase, u_window_offset);
	/*
	 * From now on, we run in primary mode, so we refrain from
	 * calling regular kernel services (e.g. like
	 * task_pid_vnr()).
	 */
	if (ret == 0 && !thread_hash(hkey, thread, pid))
		ret = -EAGAIN;

	if (ret)
		xnpod_cancel_thread(&thread->threadbase);
	else
		thread->hkey = *hkey;

	return ret ? ERR_PTR(ret) : thread;
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
	thread = thread_find_local(&hkey);

	if (__xn_safe_copy_from_user(&startt, u_startt, sizeof(startt)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&periodt, u_periodt, sizeof(periodt)))
		return -EFAULT;

	return pthread_make_periodic_np(thread, clk_id, &startt, &periodt);
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

	xnlock_get_irqsave(&nklock, s);

	thread = thread_find_local(&hkey);
	if (thread == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	snprintf(xnthread_name(&thread->threadbase),
		 XNOBJECT_NAME_LEN - 1, "%s", name);
	p = xnthread_host_task(&thread->threadbase);
	get_task_struct(p);

	xnlock_put_irqrestore(&nklock, s);

	strncpy(p->comm, name, sizeof(p->comm));
	p->comm[sizeof(p->comm) - 1] = '\0';
	put_task_struct(p);

	return 0;
}

int cobalt_thread_probe_np(pid_t pid)
{
	struct global_thread_hash *gslot;
	u32 hash;
	int ret;
	spl_t s;

	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	gslot = global_index[hash & (PTHREAD_HSLOTS - 1)];
	while (gslot && gslot->pid != pid)
		gslot = gslot->next;

	ret = gslot ? 0 : -ESRCH;

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_thread_kill(unsigned long pth, int sig)
{
	struct cobalt_sigpending *sigp;
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_find_local(&hkey);
	if (thread == NULL) {
		ret = -ESRCH;
		goto out;
	}

	/*
	 * We have undocumented pseudo-signals to suspend/resume/unblock
	 * threads, force them out of primary mode or even demote them
	 * to the weak scheduling class/priority. Process them early,
	 * before anyone can notice...
	 */
	switch(sig) {
	case 0:
		/* Check for existence only. */
		break;
	case SIGSUSP:
		/*
		 * Marking cobalt_thread_kill as __xn_exec_primary for
		 * handling self-suspension would be overkill, since
		 * no other signal would require this, so we handle
		 * that case locally here.
		 */
		if (xnshadow_current_p(&thread->threadbase) && xnpod_root_p()) {
			/*
			 * We won't vanish, so we may drop the lock
			 * while hardening.
			 */
			xnlock_put_irqrestore(&nklock, s);
			ret = xnshadow_harden();
			if (ret)
				return ret;
			xnlock_get_irqsave(&nklock, s);
		}
		xnpod_suspend_thread(&thread->threadbase, XNSUSP,
				     XN_INFINITE, XN_RELATIVE, NULL);
		if (&thread->threadbase == xnpod_current_thread() &&
		    xnthread_test_info(&thread->threadbase, XNBREAK))
			ret = EINTR;
		break;
	case SIGRESM:
		xnpod_resume_thread(&thread->threadbase, XNSUSP);
		goto resched;
	case SIGRELS:
		xnpod_unblock_thread(&thread->threadbase);
		goto resched;
	case SIGKICK:
		xnshadow_kick(&thread->threadbase);
		goto resched;
	case SIGDEMT:
		xnshadow_demote(&thread->threadbase);
		goto resched;
	case 1 ... _NSIG:
		sigp = cobalt_signal_alloc();
		if (sigp) {
			sigp->si.si_signo = sig;
			sigp->si.si_errno = 0;
			sigp->si.si_code = SI_USER;
			sigp->si.si_pid = current->pid;
			sigp->si.si_uid = current_uid();
			cobalt_signal_send(thread, sigp);
		}
	resched:
		xnpod_schedule();
		break;
	default:
		ret = -EINVAL;
	}
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_thread_stat(pid_t pid,
		       struct cobalt_threadstat __user *u_stat)
{
	struct global_thread_hash *gslot;
	struct cobalt_threadstat stat;
	struct xnthread *thread;
	xnticks_t xtime;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *)&pid, sizeof(pid) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	gslot = global_index[hash & (PTHREAD_HSLOTS - 1)];
	while (gslot && gslot->pid != pid)
		gslot = gslot->next;

	if (gslot == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	thread = &gslot->thread->threadbase;
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

	xnlock_put_irqrestore(&nklock, s);

	return __xn_safe_copy_to_user(u_stat, &stat, sizeof(stat));
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
	struct cobalt_local_hkey hkey;
	struct cobalt_thread *thread;
	struct sched_param_ex param;
	int policy, ret;

	hkey.u_pth = pth;
	hkey.mm = current->mm;
	thread = thread_find_local(&hkey);
	if (thread == NULL)
		return -ESRCH;

	ret = pthread_getschedparam_ex(thread, &policy, &param);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_policy, &policy, sizeof(int)))
		return -EFAULT;

	return __xn_safe_copy_to_user(u_param, &param, sizeof(param));
}

#ifdef CONFIG_XENO_OPT_COBALT_EXTENSION

void cobalt_thread_extend(struct cobalt_thread *thread,
			  struct cobalt_extension *ext,
			  void *priv)
{
	struct xnpersonality *prev;

	prev = xnshadow_push_personality(&thread->threadbase, &ext->core);
	cobalt_set_extref(&thread->extref, ext, priv);
	XENO_BUGON(NUCLEUS, prev != &cobalt_personality);
}
EXPORT_SYMBOL_GPL(cobalt_thread_extend);

void cobalt_thread_restrict(struct cobalt_thread *thread)
{
	xnshadow_pop_personality(&thread->threadbase, &cobalt_personality);
	cobalt_set_extref(&thread->extref, NULL, NULL);
}
EXPORT_SYMBOL_GPL(cobalt_thread_restrict);

#endif /* !CONFIG_XENO_OPT_COBALT_EXTENSION */

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
	case SCHED_NORMAL:
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
	case SCHED_NORMAL:
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
	struct sched_param_ex param;
	struct cobalt_thread *curr;
	int policy = SCHED_NORMAL;

	curr = cobalt_current_thread();
	pthread_getschedparam_ex(curr, &policy, &param);
	xnpod_yield();

	return policy == SCHED_NORMAL;
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

/*@}*/
