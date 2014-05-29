/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * @{
 */
#include <linux/types.h>
#include "internal.h"
#include "thread.h"
#include "sched.h"
#include "clock.h"
#include <trace/events/cobalt-posix.h>

struct xnsched_class *
cobalt_sched_policy_param(union xnsched_policy_param *param,
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
		if (tslice == XN_INFINITE && tslice_r)
			tslice = *tslice_r;
		/* falldown wanted */
	case SCHED_FIFO:
		if (prio < XNSCHED_FIFO_MIN_PRIO ||
		    prio > XNSCHED_FIFO_MAX_PRIO)
			return NULL;
		break;
	case SCHED_COBALT:
		if (prio < XNSCHED_CORE_MIN_PRIO ||
		    prio > XNSCHED_CORE_MAX_PRIO)
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

	if (tslice_r)
		*tslice_r = tslice;

	return sched_class;
}

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
		ret = XNSCHED_CORE_MIN_PRIO;
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
		ret = XNSCHED_CORE_MAX_PRIO;
		break;
	case SCHED_NORMAL:
		ret = 0;
		break;
	case SCHED_WEAK:
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
		ret = XNSCHED_FIFO_MAX_PRIO;
#else
		ret = 0;
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
int do_quota_config(int cpu, const union sched_config *config, size_t len)
{
	int ret = -ESRCH, quota_percent, quota_peak_percent, quota_sum;
	const struct __sched_config_quota *p = &config->quota;
	struct cobalt_sched_group *group;
	struct xnsched_quota_group *tg;
	struct cobalt_kqueues *kq;
	struct xnsched *sched;
	spl_t s;

	if (len < sizeof(*p))
		return -EINVAL;

	if (p->op == sched_quota_add) {
		group = xnmalloc(sizeof(*group));
		if (group == NULL)
			return -ENOMEM;
		tg = &group->quota;
		kq = cobalt_kqueues(0);
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		ret = xnsched_quota_create_group(tg, sched, &quota_sum);
		if (ret) {
			xnlock_put_irqrestore(&nklock, s);
			xnfree(group);
		} else {
			list_add(&group->next, &kq->schedq);
			xnlock_put_irqrestore(&nklock, s);
			ret = __xn_safe_copy_to_user(p->add.tgid_r, &tg->tgid,
						     sizeof(tg->tgid));
			if (ret == 0 && p->sum_r)
				ret = __xn_safe_copy_to_user(p->sum_r, &quota_sum,
							     sizeof(quota_sum));
		}
		return ret;
	}

	if (p->op == sched_quota_remove) {
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		tg = xnsched_quota_find_group(sched, p->remove.tgid);
		if (tg == NULL) {
			xnlock_put_irqrestore(&nklock, s);
			return ret;
		}
		group = container_of(tg, struct cobalt_sched_group, quota);
		ret = xnsched_quota_destroy_group(tg, &quota_sum);
		if (ret) {
			xnlock_put_irqrestore(&nklock, s);
			return ret;
		}
		list_del(&group->next);
		xnlock_put_irqrestore(&nklock, s);
		xnfree(group);
		if (p->sum_r)
			ret = __xn_safe_copy_to_user(p->sum_r, &quota_sum,
						     sizeof(quota_sum));
		return ret;
	}

	if (p->op == sched_quota_set) {
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		tg = xnsched_quota_find_group(sched, p->set.tgid);
		if (tg) {
			xnsched_quota_set_limit(tg,
						p->set.quota,
						p->set.quota_peak,
						&quota_sum);
			ret = 0;
		}
		xnlock_put_irqrestore(&nklock, s);
		if (ret == 0 && p->sum_r)
			ret = __xn_safe_copy_to_user(p->sum_r, &quota_sum,
						     sizeof(quota_sum));
		return ret;
	}

	if (p->op == sched_quota_get) {
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		tg = xnsched_quota_find_group(sched, p->get.tgid);
		if (tg) {
			quota_percent = tg->quota_percent;
			quota_peak_percent = tg->quota_peak_percent;
			quota_sum = xnsched_quota_sum_all(sched);
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
		if (ret == 0 && p->sum_r)
			ret = __xn_safe_copy_to_user(p->sum_r, &quota_sum,
						     sizeof(quota_sum));
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

	return do_quota_config(cpu, &buf, len);
}

#else /* !CONFIG_XENO_OPT_SCHED_QUOTA */

static inline
int do_quota_config(int cpu, const union sched_config *config, size_t len)
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
		ret = do_quota_config(cpu, buf, len);
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

int cobalt_sched_weighted_prio(int policy,
			       const struct sched_param_ex __user *u_param)
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;
	struct sched_param_ex param_ex;
	int prio;

	if (__xn_safe_copy_from_user(&param_ex, u_param, sizeof(param_ex)))
		return -EFAULT;

	sched_class = cobalt_sched_policy_param(&param, policy,
						&param_ex, NULL);
	if (sched_class == NULL)
		return -EINVAL;

	prio = param_ex.sched_priority;
	if (prio < 0)
		prio = -prio;

	return prio + sched_class->weight;
}

void cobalt_sched_cleanup(struct cobalt_kqueues *q)
{
	struct cobalt_sched_group *group;
	int quota_sum;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	for (;;) {
		if (list_empty(&q->schedq))
			break;

		group = list_get_entry(&q->schedq, struct cobalt_sched_group, next);
		xnsched_quota_destroy_group(&group->quota, &quota_sum);
		xnlock_put_irqrestore(&nklock, s);
		xnfree(group);
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_sched_pkg_init(void)
{
	INIT_LIST_HEAD(&cobalt_global_kqueues.schedq);
}

void cobalt_sched_pkg_cleanup(void)
{
	cobalt_sched_cleanup(&cobalt_global_kqueues);
}

/*@}*/
