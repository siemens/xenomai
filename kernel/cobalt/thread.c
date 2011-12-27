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
      magic:COBALT_THREAD_ATTR_MAGIC,
      detachstate:PTHREAD_CREATE_JOINABLE,
      stacksize:PTHREAD_STACK_MIN,
      inheritsched:PTHREAD_EXPLICIT_SCHED,
      policy:SCHED_OTHER,
      schedparam_ex:{
      sched_priority:0},

      name:NULL,
      fp:1,
      affinity:XNPOD_ALL_CPUS,
};

static unsigned cobalt_get_magic(void)
{
	return COBALT_SKIN_MAGIC;
}

static struct xnthread_operations cobalt_thread_ops = {
	.get_magic = &cobalt_get_magic,
};

#define PTHREAD_HSLOTS (1 << 8)	/* Must be a power of 2 */

struct tid_hash {
	pid_t tid;
	struct tid_hash *next;
};

static struct cobalt_hash *pthread_table[PTHREAD_HSLOTS];

static struct tid_hash *tid_table[PTHREAD_HSLOTS];

static inline struct cobalt_hash *
cobalt_thread_hash(const struct cobalt_hkey *hkey, pthread_t k_tid, pid_t h_tid)
{
	struct cobalt_hash **pthead, *ptslot;
	struct tid_hash **tidhead, *tidslot;
	u32 hash;
	void *p;
	spl_t s;

	p = xnmalloc(sizeof(*ptslot) + sizeof(*tidslot));
	if (p == NULL)
		return NULL;

	ptslot = p;
	ptslot->hkey = *hkey;
	ptslot->k_tid = k_tid;
	ptslot->h_tid = h_tid;
	hash = jhash2((u32 *)&ptslot->hkey,
		      sizeof(ptslot->hkey) / sizeof(u32), 0);
	pthead = &pthread_table[hash & (PTHREAD_HSLOTS - 1)];

	tidslot = p + sizeof(*ptslot);
	tidslot->tid = h_tid;
	hash = jhash2((u32 *)&h_tid, sizeof(h_tid) / sizeof(u32), 0);
	tidhead = &tid_table[hash & (PTHREAD_HSLOTS - 1)];

	xnlock_get_irqsave(&nklock, s);
	ptslot->next = *pthead;
	*pthead = ptslot;
	tidslot->next = *tidhead;
	*tidhead = tidslot;
	xnlock_put_irqrestore(&nklock, s);

	return ptslot;
}

static inline void cobalt_thread_unhash(const struct cobalt_hkey *hkey)
{
	struct cobalt_hash **pttail, *ptslot;
	struct tid_hash **tidtail, *tidslot;
	pid_t h_tid;
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
	h_tid = ptslot->h_tid;
	hash = jhash2((u32 *)&h_tid, sizeof(h_tid) / sizeof(u32), 0);
	tidtail = &tid_table[hash & (PTHREAD_HSLOTS - 1)];
	tidslot = *tidtail;
	while (tidslot && tidslot->tid != h_tid) {
		tidtail = &tidslot->next;
		tidslot = *tidtail;
	}
	/* tidslot must be found here. */
	XENO_BUGON(POSIX, !(tidslot && tidtail));
	*tidtail = tidslot->next;

	xnlock_put_irqrestore(&nklock, s);

	xnfree(ptslot);
	xnfree(tidslot);
}

pthread_t cobalt_thread_find(const struct cobalt_hkey *hkey)
{
	struct cobalt_hash *ptslot;
	pthread_t k_tid;
	u32 hash;
	spl_t s;

	hash = jhash2((u32 *) hkey, sizeof(*hkey) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	ptslot = pthread_table[hash & (PTHREAD_HSLOTS - 1)];

	while (ptslot != NULL &&
	       (ptslot->hkey.u_tid != hkey->u_tid || ptslot->hkey.mm != hkey->mm))
		ptslot = ptslot->next;

	k_tid = ptslot ? ptslot->k_tid : NULL;

	xnlock_put_irqrestore(&nklock, s);

	return k_tid;
}

static void thread_destroy(pthread_t thread)
{
	removeq(thread->container, &thread->link);
	xnsynch_destroy(&thread->monitor_synch);
	xnheap_schedule_free(&kheap, thread, &thread->link);
}

static void thread_delete_hook(xnthread_t *xnthread)
{
	pthread_t thread = thread2pthread(xnthread);
	spl_t s;

	if (!thread)
		return;

	xnlock_get_irqsave(&nklock, s);

	cobalt_mark_deleted(thread);
	cobalt_timer_cleanup_thread(thread);
	thread_destroy(thread);

	xnlock_put_irqrestore(&nklock, s);

	if (xnthread_test_state(xnthread, XNSHADOW)) {
		cobalt_thread_unhash(&thread->hkey);
		if (xnthread_test_state(xnthread, XNMAPPED))
			xnshadow_unmap(xnthread);
	}
}

/**
 * Get the scheduling policy and parameters of the specified thread.
 *
 * This service returns, at the addresses @a pol and @a par, the current
 * scheduling policy and scheduling parameters (i.e. priority) of the Xenomai
 * POSIX skin thread @a tid. If this service is called from user-space and @a
 * tid is not the identifier of a Xenomai POSIX skin thread, this service
 * fallback to Linux regular pthread_getschedparam service.
 *
 * @param tid target thread;
 *
 * @param pol address where the scheduling policy of @a tid is stored on
 * success;
 *
 * @param par address where the scheduling parameters of @a tid is stored on
 * success.
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
pthread_getschedparam(pthread_t tid, int *pol, struct sched_param *par)
{
	int prio;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(tid, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	prio = xnthread_base_priority(&tid->threadbase);
	par->sched_priority = prio;
	*pol = tid->sched_policy;

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Get the extended scheduling policy and parameters of the specified
 * thread.
 *
 * This service is an extended version of pthread_getschedparam(),
 * that also supports Xenomai-specific or additional POSIX scheduling
 * policies, which are not available with the host Linux environment.
 *
 * Typically, SCHED_SPORADIC parameters can be retrieved from this
 * call.
 *
 * @param tid target thread;
 *
 * @param pol address where the scheduling policy of @a tid is stored on
 * success;
 *
 * @param par address where the scheduling parameters of @a tid is stored on
 * success.
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
	prio = xnthread_base_priority(thread);
	par->sched_priority = prio;
	*pol = tid->sched_policy;

	if (base_class == &xnsched_class_rt) {
		if (xnthread_test_state(thread, XNRRB))
			ns2ts(&par->sched_rr_quantum, xnthread_time_slice(thread));
		goto unlock_and_exit;
	}

#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	if (base_class == &xnsched_class_sporadic) {
		par->sched_ss_low_priority = thread->pss->param.low_prio;
		ns2ts(&par->sched_ss_repl_period, thread->pss->param.repl_period);
		ns2ts(&par->sched_ss_init_budget, thread->pss->param.init_budget);
		par->sched_ss_max_repl = thread->pss->param.max_repl;
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
static inline int pthread_create(pthread_t *tid, const pthread_attr_t * attr)
{
	union xnsched_policy_param param;
	struct xnthread_init_attr iattr;
	pthread_t thread, cur;
	xnflags_t flags = 0;
	size_t stacksize;
	const char *name;
	int prio, ret;
	spl_t s;

	if (attr && attr->magic != COBALT_THREAD_ATTR_MAGIC)
		return -EINVAL;

	thread = (pthread_t)xnmalloc(sizeof(*thread));

	if (!thread)
		return -EAGAIN;

	thread->attr = attr ? *attr : default_thread_attr;

	cur = cobalt_current_thread();

	if (thread->attr.inheritsched == PTHREAD_INHERIT_SCHED) {
		/* cur may be NULL if pthread_create is not called by a cobalt
		   thread, in which case trying to inherit scheduling
		   parameters is treated as an error. */

		if (!cur) {
			xnfree(thread);
			return -EINVAL;
		}

		pthread_getschedparam_ex(cur, &thread->attr.policy,
					 &thread->attr.schedparam_ex);
	}

	prio = thread->attr.schedparam_ex.sched_priority;
	stacksize = thread->attr.stacksize;
	name = thread->attr.name;

	if (thread->attr.fp)
		flags |= XNFPU;

	flags |= XNSHADOW;

	iattr.name = name;
	iattr.flags = flags;
	iattr.ops = &cobalt_thread_ops;
	iattr.stacksize = stacksize;
	param.rt.prio = prio;

	if (xnpod_init_thread(&thread->threadbase,
			      &iattr, &xnsched_class_rt, &param) != 0) {
		xnfree(thread);
		return -EAGAIN;
	}

	thread->attr.name = xnthread_name(&thread->threadbase);

	inith(&thread->link);

	thread->magic = COBALT_THREAD_MAGIC;
	xnsynch_init(&thread->monitor_synch, XNSYNCH_FIFO, NULL);
	inith(&thread->monitor_link);
	thread->monitor_queued = 0;
	thread->sched_policy = thread->attr.policy;

	cobalt_timer_init_thread(thread);

	if (thread->attr.policy == SCHED_RR)
		xnpod_set_thread_tslice(&thread->threadbase, cobalt_time_slice);

	xnlock_get_irqsave(&nklock, s);
	thread->container = &cobalt_kqueues(0)->threadq;
	appendq(thread->container, &thread->link);
	xnlock_put_irqrestore(&nklock, s);

#ifndef __XENO_SIM__
	thread->hkey.u_tid = 0;
	thread->hkey.mm = NULL;
#endif

	/* We need an anonymous registry entry to obtain a handle for fast
	   mutex locking. */
	ret = xnthread_register(&thread->threadbase, "");
	if (ret) {
		thread_destroy(thread);
		return ret;
	}

	*tid = thread;		/* Must be done before the thread is started. */

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
	int err;
	spl_t s;

	if (clock_id != CLOCK_MONOTONIC &&
	    clock_id != CLOCK_MONOTONIC_RAW &&
	    clock_id != CLOCK_REALTIME)
		return ENOTSUP;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

	start = ts2ns(starttp);
	period = ts2ns(periodtp);
	err = xnpod_set_thread_periodic(&thread->threadbase, start,
					clock_flag(TIMER_ABSTIME, clock_id),
					period);
      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
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
 * to primary mode. Any other use either cause to a nop, or an error.
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
 */
static inline int pthread_set_mode_np(int clrmask, int setmask, int *mode_r)
{
	xnthread_t *cur = xnpod_current_thread();
	xnflags_t valid_flags = XNLOCK, old;

#ifndef __XENO_SIM__
	if (xnthread_test_state(cur, XNSHADOW))
		valid_flags |= XNTHREAD_STATE_SPARE1 | XNTRAPSW;
#endif

	/* XNTHREAD_STATE_SPARE1 is used as the CONFORMING mode bit. */

	if ((clrmask & ~valid_flags) != 0 || (setmask & ~valid_flags) != 0)
		return -EINVAL;

	old = xnpod_set_thread_mode(cur,
				    clrmask & ~XNTHREAD_STATE_SPARE1,
				    setmask & ~XNTHREAD_STATE_SPARE1);
	if (mode_r)
		*mode_r = old;

	if ((clrmask & ~setmask) & XNLOCK)
		/* Reschedule if the scheduler has been unlocked. */
		xnpod_schedule();

	return 0;
}

/**
 * Set a thread name.
 *
 * This service set to @a name, the name of @a thread. This name is used for
 * displaying information in /proc/xenomai/sched.
 *
 * This service is a non-portable extension of the POSIX interface.
 *
 * @param thread target thread;
 *
 * @param name name of the thread.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 *
 */
static inline int pthread_set_name_np(pthread_t thread, const char *name)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(thread, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	snprintf(xnthread_name(&thread->threadbase),
		 XNOBJECT_NAME_LEN, "%s", name);

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Set the scheduling policy and parameters of the specified thread.
 *
 * This service set the scheduling policy of the Xenomai POSIX skin thread @a
 * tid to the value @a  pol, and its scheduling parameters (i.e. its priority)
 * to the value pointed to by @a par.
 *
 * When used in user-space, passing the current thread ID as @a tid argument,
 * this service turns the current thread into a Xenomai POSIX skin thread. If @a
 * tid is neither the identifier of the current thread nor the identifier of a
 * Xenomai POSIX skin thread this service falls back to the regular
 * pthread_setschedparam() service, hereby causing the current thread to switch
 * to secondary mode if it is Xenomai thread.
 *
 * @param tid target thread;
 *
 * @param pol scheduling policy, one of SCHED_FIFO, SCHED_COBALT,
 * SCHED_RR or SCHED_OTHER;
 *
 * @param par scheduling parameters address.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid;
 * - EINVAL, @a pol or @a par->sched_priority is invalid;
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
 */
static inline int
pthread_setschedparam(pthread_t tid, int pol, const struct sched_param *par)
{
	union xnsched_policy_param param;
	struct xnthread *thread;
	xnticks_t tslice;
	int prio;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(tid, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	thread = &tid->threadbase;
	prio = par->sched_priority;
	tslice = XN_INFINITE;

	switch (pol) {
	case SCHED_OTHER:
		if (prio)
			goto fail;
		break;
	case SCHED_RR:
		tslice = xnthread_time_slice(thread);
		if (tslice == XN_INFINITE)
			tslice = cobalt_time_slice;
		/* falldown wanted */
	case SCHED_FIFO:
	case SCHED_SPORADIC:
		if (prio < COBALT_MIN_PRIORITY || prio > COBALT_MAX_PRIORITY)
			goto fail;
		break;
	case SCHED_COBALT:
		if (prio < COBALT_MIN_PRIORITY || prio > XNSCHED_RT_MAX_PRIO)
			goto fail;
		break;
	default:
	fail:
		xnlock_put_irqrestore(&nklock, s);
		return -EINVAL;
	}

	xnpod_set_thread_tslice(thread, tslice);

	tid->sched_policy = pol;
	param.rt.prio = prio;
	xnpod_set_thread_schedparam(thread, &xnsched_class_rt, &param);

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Set the extended scheduling policy and parameters of the specified
 * thread.
 *
 * This service is an extended version of pthread_setschedparam(),
 * that supports Xenomai-specific or additional scheduling policies,
 * which are not available with the host Linux environment.
 *
 * Typically, a Xenomai thread policy can be set to SCHED_SPORADIC
 * using this call.
 *
 * @param tid target thread;
 *
 * @param pol address where the scheduling policy of @a tid is stored on
 * success;
 *
 * @param par address where the scheduling parameters of @a tid is stored on
 * success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid.
 * - EINVAL, @a par contains invalid parameters.
 * - ENOMEM, lack of memory to perform the operation.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 *
 */
static inline int pthread_setschedparam_ex(pthread_t tid, int pol,
					   const struct sched_param_ex *par)
{
	union xnsched_policy_param param;
	struct sched_param short_param;
	xnticks_t tslice;
	int ret = 0;
	spl_t s;

	switch (pol) {
	case SCHED_OTHER:
	case SCHED_FIFO:
	case SCHED_COBALT:
		xnpod_set_thread_tslice(&tid->threadbase, XN_INFINITE);
		short_param.sched_priority = par->sched_priority;
		return pthread_setschedparam(tid, pol, &short_param);
	default:
		if (par->sched_priority < COBALT_MIN_PRIORITY ||
		    par->sched_priority >  COBALT_MAX_PRIORITY) {
			return EINVAL;
		}
	}

	xnlock_get_irqsave(&nklock, s);

	if (!cobalt_obj_active(tid, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	switch (pol) {
	case SCHED_RR:
		tslice = ts2ns(&par->sched_rr_quantum);
		ret = xnpod_set_thread_tslice(&tid->threadbase, tslice);
		break;
	default:

		xnlock_put_irqrestore(&nklock, s);
		return -EINVAL;

#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	case SCHED_SPORADIC:
		xnpod_set_thread_tslice(&tid->threadbase, XN_INFINITE);
		param.pss.normal_prio = par->sched_priority;
		param.pss.low_prio = par->sched_ss_low_priority;
		param.pss.current_prio = param.pss.normal_prio;
		param.pss.init_budget = ts2ns(&par->sched_ss_init_budget);
		param.pss.repl_period = ts2ns(&par->sched_ss_repl_period);
		param.pss.max_repl = par->sched_ss_max_repl;
		ret = xnpod_set_thread_schedparam(&tid->threadbase,
						  &xnsched_class_sporadic, &param);
		break;
#else
		(void)param;
#endif
	}

	tid->sched_policy = pol;

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return -ret;
}

int cobalt_thread_setschedparam(unsigned long tid,
				int policy,
				struct sched_param __user *u_param,
				unsigned long __user *u_mode_offset,
				int __user *u_promoted)
{
	struct sched_param param;
	struct cobalt_hkey hkey;
	int err, promoted = 0;
	pthread_t k_tid;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	if (!k_tid && u_mode_offset) {
		/*
		 * If the syscall applies to "current", and the latter
		 * is not a Xenomai thread already, then shadow it.
		 */
		k_tid = cobalt_thread_shadow(current, &hkey, u_mode_offset);
		if (IS_ERR(k_tid))
			return PTR_ERR(k_tid);

		promoted = 1;
	}
	if (k_tid)
		err = pthread_setschedparam(k_tid, policy, &param);
	else
		/*
		 * target thread is not a real-time thread, and is not current,
		 * so can not be promoted, try again with the real
		 * pthread_setschedparam service.
		 */
		err = -EPERM;

	if (err == 0 &&
	    __xn_safe_copy_to_user(u_promoted, &promoted, sizeof(promoted)))
		err = -EFAULT;

	return err;
}

int cobalt_thread_setschedparam_ex(unsigned long tid,
				   int policy,
				   struct sched_param __user *u_param,
				   unsigned long __user *u_mode_offset,
				   int __user *u_promoted)
{
	struct sched_param_ex param;
	struct cobalt_hkey hkey;
	int err, promoted = 0;
	pthread_t k_tid;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	if (!k_tid && u_mode_offset) {
		k_tid = cobalt_thread_shadow(current, &hkey, u_mode_offset);
		if (IS_ERR(k_tid))
			return PTR_ERR(k_tid);

		promoted = 1;
	}
	if (k_tid)
		err = pthread_setschedparam_ex(k_tid, policy, &param);
	else
		err = -EPERM;

	if (err == 0 &&
	    __xn_safe_copy_to_user(u_promoted, &promoted, sizeof(promoted)))
		err = -EFAULT;

	return err;
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
			 unsigned long __user *u_mode)
{
	struct task_struct *p = current;
	struct sched_param_ex param;
	struct cobalt_hkey hkey;
	pthread_attr_t attr;
	pthread_t k_tid;
	pid_t h_tid;
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

	ret = pthread_create(&k_tid, &attr);
	if (ret)
		return ret;

	h_tid = task_pid_vnr(p);
	ret = xnshadow_map(&k_tid->threadbase, NULL, u_mode);
	if (ret)
		goto fail;

	if (!cobalt_thread_hash(&hkey, k_tid, h_tid)) {
		ret = -ENOMEM;
		goto fail;
	}

	k_tid->hkey = hkey;

	return 0;

fail:
	xnpod_delete_thread(&k_tid->threadbase);

	return ret;
}

pthread_t cobalt_thread_shadow(struct task_struct *p,
			       struct cobalt_hkey *hkey,
			       unsigned long __user *u_mode_offset)
{
	pthread_attr_t attr;
	pthread_t k_tid;
	pid_t h_tid;
	int err;

	attr = default_thread_attr;
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.name = p->comm;

	err = pthread_create(&k_tid, &attr);

	if (err)
		return ERR_PTR(-err);

	h_tid = task_pid_vnr(p);
	err = xnshadow_map(&k_tid->threadbase, NULL, u_mode_offset);
	/*
	 * From now on, we run in primary mode, so we refrain from
	 * calling regular kernel services (e.g. like
	 * task_pid_vnr()).
	 */
	if (err == 0 && !cobalt_thread_hash(hkey, k_tid, h_tid))
		err = -EAGAIN;

	if (err)
		xnpod_delete_thread(&k_tid->threadbase);
	else
		k_tid->hkey = *hkey;

	return err ? ERR_PTR(err) : k_tid;
}

int cobalt_thread_make_periodic_np(unsigned long tid,
				   clockid_t clk_id,
				   struct timespec __user *u_startt,
				   struct timespec __user *u_periodt)
{
	struct timespec startt, periodt;
	struct cobalt_hkey hkey;
	pthread_t k_tid;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	if (__xn_safe_copy_from_user(&startt, u_startt, sizeof(startt)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&periodt, u_periodt, sizeof(periodt)))
		return -EFAULT;

	return pthread_make_periodic_np(k_tid, clk_id, &startt, &periodt);
}

int cobalt_thread_wait_np(unsigned long __user *u_overruns)
{
	unsigned long overruns;
	int err;

	err = xnpod_wait_thread_period(&overruns);

	if (u_overruns && (err == 0 || err == -ETIMEDOUT))
		__xn_put_user(overruns, u_overruns);

	return err;
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
	pthread_t k_tid;

	if (__xn_safe_strncpy_from_user(name, u_name,
					sizeof(name) - 1) < 0)
		return -EFAULT;

	name[sizeof(name) - 1] = '\0';

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	return pthread_set_name_np(k_tid, name);
}

int cobalt_thread_probe_np(pid_t h_tid)
{
	struct tid_hash *tidslot;
	u32 hash;
	int ret;
	spl_t s;

	hash = jhash2((u32 *)&h_tid, sizeof(h_tid) / sizeof(u32), 0);

	xnlock_get_irqsave(&nklock, s);

	tidslot = tid_table[hash & (PTHREAD_HSLOTS - 1)];
	while (tidslot && tidslot->tid != h_tid)
		tidslot = tidslot->next;

	ret = tidslot ? 0 : -ESRCH;

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

int cobalt_thread_kill(unsigned long tid, int sig)
{
	struct cobalt_hkey hkey;
	pthread_t k_tid;
	int ret;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	if (!k_tid)
		return -ESRCH;
	/*
	 * We have to take care of self-suspension, when the
	 * underlying shadow thread is currently relaxed. In that
	 * case, we must switch back to primary before issuing the
	 * suspend call to the nucleus in pthread_kill(). Marking the
	 * cobalt_thread_kill syscall as __xn_exec_primary would be
	 * overkill, since no other signal would require this, so we
	 * handle that case locally here.
	 */
	if (sig == SIGSUSP && xnpod_current_p(&k_tid->threadbase)) {
		if (!xnpod_shadow_p()) {
			ret = xnshadow_harden();
			if (ret)
				return ret;
		}
	}

	switch(sig) {
	/*
	 * Undocumented pseudo-signals to suspend/resume/unblock
	 * threads, force them out of primary mode or even demote them
	 * to the SCHED_OTHER class. Process them early, before anyone
	 * can notice...
	 */
	case SIGSUSP:
		/*
		 * The self-suspension case for shadows was handled at
		 * call site: we must be in primary mode already.
		 */
		xnpod_suspend_thread(&k_tid->threadbase, XNSUSP,
				     XN_INFINITE, XN_RELATIVE, NULL);
		if (&k_tid->threadbase == xnpod_current_thread() &&
		    xnthread_test_info(&k_tid->threadbase, XNBREAK))
			ret = EINTR;
		break;

	case SIGRESM:
		xnpod_resume_thread(&k_tid->threadbase, XNSUSP);
		goto resched;

	case SIGRELS:
		xnpod_unblock_thread(&k_tid->threadbase);
		goto resched;

	case SIGKICK:
		xnshadow_kick(&k_tid->threadbase);
		goto resched;

	case SIGDEMT:
		xnshadow_demote(&k_tid->threadbase);
	  resched:
		xnpod_schedule();
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int cobalt_thread_stat(unsigned long tid,
		       struct cobalt_threadstat __user *u_stat)
{
	struct cobalt_threadstat stat;
	struct cobalt_hkey hkey;
	struct xnthread *thread;
	pthread_t k_tid;
	xnticks_t xtime;
	spl_t s;

	hkey.u_tid = tid;
	hkey.mm = current->mm;

	xnlock_get_irqsave(&nklock, s);

	k_tid = cobalt_thread_find(&hkey);
	if (k_tid == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	thread = &k_tid->threadbase;
	xtime = xnthread_get_exectime(thread);
	if (xnthread_sched(thread)->curr == thread)
		xtime += xnstat_exectime_now() - xnthread_get_lastswitch(thread);
	stat.xtime = xnarch_tsc_to_ns(xtime);
	stat.msw = xnstat_counter_get(&thread->stat.ssw);
	stat.csw = xnstat_counter_get(&thread->stat.csw);
	stat.xsc = xnstat_counter_get(&thread->stat.xsc);
	stat.pf = xnstat_counter_get(&thread->stat.pf);
	stat.status = xnthread_state_flags(thread);

	xnlock_put_irqrestore(&nklock, s);

	return __xn_safe_copy_to_user(u_stat, &stat, sizeof(stat));
}

int cobalt_thread_getschedparam(unsigned long tid,
				int __user *u_policy,
				struct sched_param __user *u_param)
{
	struct sched_param param;
	struct cobalt_hkey hkey;
	pthread_t k_tid;
	int policy, err;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	if (!k_tid)
		return -ESRCH;

	err = pthread_getschedparam(k_tid, &policy, &param);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_policy, &policy, sizeof(int)))
		return -EFAULT;

	return __xn_safe_copy_to_user(u_param, &param, sizeof(param));
}

int cobalt_thread_getschedparam_ex(unsigned long tid,
				   int __user *u_policy,
				   struct sched_param __user *u_param)
{
	struct sched_param_ex param;
	struct cobalt_hkey hkey;
	pthread_t k_tid;
	int policy, err;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = cobalt_thread_find(&hkey);

	if (!k_tid)
		return -ESRCH;

	err = pthread_getschedparam_ex(k_tid, &policy, &param);
	if (err)
		return err;

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
	case SCHED_COBALT:
		return COBALT_MIN_PRIORITY;

	case SCHED_OTHER:
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
		return COBALT_MAX_PRIORITY;

	case SCHED_COBALT:
		return XNSCHED_RT_MAX_PRIO;

	case SCHED_OTHER:
		return 0;

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

void cobalt_threadq_cleanup(cobalt_kqueues_t *q)
{
	xnholder_t *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getheadq(&q->threadq)) != NULL) {
		pthread_t thread = link2pthread(holder);

		/* Enter the abort state (see xnpod_abort_thread()). */
		if (!xnpod_current_p(&thread->threadbase))
			xnpod_suspend_thread(&thread->threadbase, XNDORMANT,
					     XN_INFINITE, XN_RELATIVE, NULL);
		if (cobalt_obj_active
		    (thread, COBALT_THREAD_MAGIC, struct cobalt_thread)) {
			/* Remaining running thread. */
			xnpod_delete_thread(&thread->threadbase);
		} else
			/* Remaining TCB (joinable thread, which was never joined). */
			thread_destroy(thread);
		xnlock_put_irqrestore(&nklock, s);
#if XENO_DEBUG(POSIX)
		xnprintf("POSIX: destroyed thread %p\n", thread);
#endif /* XENO_DEBUG(POSIX) */
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_thread_pkg_init(u_long rrperiod)
{
	initq(&cobalt_global_kqueues.threadq);
	cobalt_time_slice = rrperiod;
	xnpod_add_hook(XNHOOK_THREAD_DELETE, thread_delete_hook);
}

void cobalt_thread_pkg_cleanup(void)
{
	cobalt_threadq_cleanup(&cobalt_global_kqueues);
	xnpod_remove_hook(XNHOOK_THREAD_DELETE, thread_delete_hook);
}

/*@}*/
