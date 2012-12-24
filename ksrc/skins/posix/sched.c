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
 * @ingroup posix_thread
 * @defgroup posix_sched Threads scheduling services.
 *
 * Thread scheduling services.
 *
 * Xenomai POSIX skin supports the scheduling policies SCHED_FIFO,
 * SCHED_RR, SCHED_SPORADIC, SCHED_TP and SCHED_OTHER.
 *
 * The SCHED_OTHER policy is mainly useful for user-space non-realtime
 * activities that need to synchronize with real-time activities.
 *
 * The SCHED_RR policy is only effective if the time base is periodic
 * (i.e. if configured with the compilation constant @a
 * CONFIG_XENO_OPT_POSIX_PERIOD or the @a xeno_nucleus module
 * parameter @a tick_arg set to a non null value). The SCHED_RR
 * round-robin time slice is configured with the @a xeno_posix module
 * parameter @a time_slice, as a count of system timer clock ticks.
 *
 * The SCHED_SPORADIC policy provides a mean to schedule aperiodic or
 * sporadic threads in periodic-based systems.
 *
 * The SCHED_TP policy divides the scheduling time into a recurring
 * global frame, which is itself divided into an arbitrary number of
 * time partitions. Only threads assigned to the current partition are
 * deemed runnable, and scheduled according to a FIFO-based rule
 * within this partition. When completed, the current partition is
 * advanced automatically to the next one by the scheduler, and the
 * global time frame recurs from the first partition defined, when the
 * last partition has ended.
 *
 * The scheduling policy and priority of a thread is set when creating a thread,
 * by using thread creation attributes (see pthread_attr_setinheritsched(),
 * pthread_attr_setschedpolicy() and pthread_attr_setschedparam()), or when the
 * thread is already running by using the service pthread_setschedparam().
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_08.html#tag_02_08_04">
 * Specification.</a>
 *
 *@{*/

#include <posix/thread.h>

/**
 * Get minimum priority of the specified scheduling policy.
 *
 * This service returns the minimum priority of the scheduling policy @a
 * policy.
 *
 * @param policy scheduling policy, one of SCHED_FIFO, SCHED_RR,
 * SCHED_SPORADIC, SCHED_TP or SCHED_OTHER.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_min.html">
 * Specification.</a>
 *
 */
int sched_get_priority_min(int policy)
{
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_SPORADIC:
	case SCHED_TP:
		return PSE51_MIN_PRIORITY;

	case SCHED_OTHER:
		return 0;

	default:
		thread_set_errno(EINVAL);
		return -1;
	}
}

/**
 * Get maximum priority of the specified scheduling policy.
 *
 * This service returns the maximum priority of the scheduling policy @a
 * policy.
 *
 * @param policy scheduling policy, one of SCHED_FIFO, SCHED_RR,
 * SCHED_SPORADIC, SCHED_TP or SCHED_OTHER.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_max.html">
 * Specification.</a>
 *
 */
int sched_get_priority_max(int policy)
{
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_SPORADIC:
	case SCHED_TP:
		return PSE51_MAX_PRIORITY;

	case SCHED_OTHER:
		return 0;

	default:
		thread_set_errno(EINVAL);
		return -1;
	}
}

/**
 * Get the round-robin scheduling time slice.
 *
 * This service returns the time quantum used by Xenomai POSIX skin SCHED_RR
 * scheduling policy.
 *
 * In kernel-space, this service only works if pid is zero, in user-space,
 * round-robin scheduling policy is not supported, and this service not
 * implemented.
 *
 * @param pid must be zero;
 *
 * @param interval address where the round-robin scheduling time quantum will
 * be returned on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - ESRCH, @a pid is invalid (not 0).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_rr_get_interval.html">
 * Specification.</a>
 *
 */
int sched_rr_get_interval(int pid, struct timespec *interval)
{
	/* The only valid pid is 0. */
	if (pid) {
		thread_set_errno(ESRCH);
		return -1;
	}

	ticks2ts(interval, pse51_time_slice);

	return 0;
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
int pthread_getschedparam(pthread_t tid, int *pol, struct sched_param *par)
{
	int prio;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!pse51_obj_active(tid, PSE51_THREAD_MAGIC, struct pse51_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	prio = xnthread_base_priority(&tid->threadbase);
	par->sched_priority = prio;
	*pol = (prio
		? (!xnthread_test_state(&tid->threadbase, XNRRB)
		   ? SCHED_FIFO : SCHED_RR) : SCHED_OTHER);

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
 * Typically, SCHED_SPORADIC or SCHED_TP parameters can be retrieved
 * from this call.
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
int pthread_getschedparam_ex(pthread_t tid, int *pol, struct sched_param_ex *par)
{
	struct xnsched_class *base_class;
	struct xnthread *thread;
	int prio;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!pse51_obj_active(tid, PSE51_THREAD_MAGIC, struct pse51_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	thread = &tid->threadbase;
	base_class = xnthread_base_class(thread);
	prio = xnthread_base_priority(thread);
	par->sched_priority = prio;

	if (base_class == &xnsched_class_rt) {
		*pol = (prio
			? (!xnthread_test_state(thread, XNRRB)
			   ? SCHED_FIFO : SCHED_RR) : SCHED_OTHER);
		goto unlock_and_exit;
	}

#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	if (base_class == &xnsched_class_sporadic) {
		*pol = SCHED_SPORADIC;
		par->sched_ss_low_priority = thread->pss->param.low_prio;
		ticks2ts(&par->sched_ss_repl_period, thread->pss->param.repl_period);
		ticks2ts(&par->sched_ss_init_budget, thread->pss->param.init_budget);
		par->sched_ss_max_repl = thread->pss->param.max_repl;
		goto unlock_and_exit;
	}
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	if (base_class == &xnsched_class_tp) {
		*pol = SCHED_TP;
		par->sched_tp_partition = thread->tps - thread->sched->tp.partitions;
		goto unlock_and_exit;
	}
#endif

unlock_and_exit:

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
 * @param pol scheduling policy, one of SCHED_FIFO, SCHED_RR,
 * SCHED_SPORADIC, SCHED_TP or SCHED_OTHER;
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
int pthread_setschedparam(pthread_t tid, int pol, const struct sched_param *par)
{
	union xnsched_policy_param param;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!pse51_obj_active(tid, PSE51_THREAD_MAGIC, struct pse51_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	switch (pol) {
	default:

		xnlock_put_irqrestore(&nklock, s);
		return EINVAL;

	case SCHED_OTHER:
	case SCHED_FIFO:
	case SCHED_SPORADIC:
	case SCHED_TP:
		xnpod_set_thread_tslice(&tid->threadbase, XN_INFINITE);
		break;


	case SCHED_RR:
		xnpod_set_thread_tslice(&tid->threadbase, pse51_time_slice);
	}

	if ((pol != SCHED_OTHER && (par->sched_priority < PSE51_MIN_PRIORITY
				    || par->sched_priority >
				    PSE51_MAX_PRIORITY))
	    || (pol == SCHED_OTHER && par->sched_priority != 0)) {
		xnlock_put_irqrestore(&nklock, s);
		return EINVAL;
	}

	param.rt.prio = par->sched_priority;
	xnpod_set_thread_schedparam(&tid->threadbase, &xnsched_class_rt, &param);

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Set the extended scheduling policy and parameters of the specified
 * thread.
 *
 * This service is an extended version of pthread_setschedparam(),
 * that supports Xenomai-specific or additional POSIX scheduling
 * policies, which are not available with the host Linux environment.
 *
 * Typically, a Xenomai thread policy can be set to SCHED_SPORADIC or
 * SCHED_TP using this call.
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
int pthread_setschedparam_ex(pthread_t tid, int pol, const struct sched_param_ex *par)
{
	union xnsched_policy_param param;
	struct sched_param short_param;
	int ret = 0;
	spl_t s;

	switch (pol) {
	case SCHED_OTHER:
	case SCHED_FIFO:
	case SCHED_RR:
		short_param.sched_priority = par->sched_priority;
		return pthread_setschedparam(tid, pol, &short_param);
	default:
		if (par->sched_priority < PSE51_MIN_PRIORITY ||
		    par->sched_priority >  PSE51_MAX_PRIORITY) {
			return EINVAL;
		}
	}

	xnlock_get_irqsave(&nklock, s);

	if (!pse51_obj_active(tid, PSE51_THREAD_MAGIC, struct pse51_thread)) {
		xnlock_put_irqrestore(&nklock, s);
		return ESRCH;
	}

	switch (pol) {
	default:

		xnlock_put_irqrestore(&nklock, s);
		return EINVAL;

#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	case SCHED_SPORADIC:
		xnpod_set_thread_tslice(&tid->threadbase, XN_INFINITE);
		param.pss.normal_prio = par->sched_priority;
		param.pss.low_prio = par->sched_ss_low_priority;
		param.pss.current_prio = param.pss.normal_prio;
		param.pss.init_budget = ts2ticks_ceil(&par->sched_ss_init_budget);
		param.pss.repl_period = ts2ticks_ceil(&par->sched_ss_repl_period);
		param.pss.max_repl = par->sched_ss_max_repl;
		ret = -xnpod_set_thread_schedparam(&tid->threadbase,
						   &xnsched_class_sporadic, &param);
		break;
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	case SCHED_TP:
		xnpod_set_thread_tslice(&tid->threadbase, XN_INFINITE);
		param.tp.prio = par->sched_priority;
		param.tp.ptid = par->sched_tp_partition;
		ret = -xnpod_set_thread_schedparam(&tid->threadbase,
						   &xnsched_class_tp, &param);
		break;
#endif
	}

	(void)param;

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Yield the processor.
 *
 * This function move the current thread at the end of its priority group.
 *
 * @retval 0
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_yield.html">
 * Specification.</a>
 *
 */
int sched_yield(void)
{
	xnpod_yield();
	return 0;
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
		offset = ts2ticks_ceil(&p->offset);
		if (offset != next_offset)
			goto cleanup_and_fail;

		duration = ts2ticks_ceil(&p->duration);
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
	return EINVAL;
}

#else /* !CONFIG_XENO_OPT_SCHED_TP */

static inline
int set_tp_config(int cpu, union sched_config *config, size_t len)
{
	return EINVAL;
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
int sched_setconfig_np(int cpu, int policy,
		       union sched_config *config, size_t len)
{
	int ret;

	switch (policy)	{
	case SCHED_TP:
		ret = set_tp_config(cpu, config, len);
		break;
	default:
		ret = EINVAL;
	}

	return ret;
}

/*@}*/

EXPORT_SYMBOL_GPL(sched_get_priority_min);
EXPORT_SYMBOL_GPL(sched_get_priority_max);
EXPORT_SYMBOL_GPL(sched_rr_get_interval);
EXPORT_SYMBOL_GPL(pthread_getschedparam);
EXPORT_SYMBOL_GPL(pthread_getschedparam_ex);
EXPORT_SYMBOL_GPL(pthread_setschedparam);
EXPORT_SYMBOL_GPL(pthread_setschedparam_ex);
EXPORT_SYMBOL_GPL(sched_yield);
EXPORT_SYMBOL_GPL(sched_setconfig_np);
