/**
 * @file
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/types.h>
#include <linux/err.h>
#include <asm/xenomai/wrappers.h>
#include <nucleus/ppd.h>
#include <nucleus/sys_ppd.h>
#include <nucleus/assert.h>
#include <cobalt/syscall.h>
#include <cobalt/posix.h>
#include "thread.h"
#include "mutex.h"
#include "cond.h"
#include "mq.h"
#include "registry.h"	/* For COBALT_MAXNAME. */
#include "sem.h"
#include "shm.h"
#include "timer.h"
#include "monitor.h"
#include <rtdm/rtdm_driver.h>
#define RTDM_FD_MAX CONFIG_XENO_OPT_RTDM_FILDES

int cobalt_muxid;

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

static int __pthread_create(unsigned long tid, int policy,
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
	pthread_attr_init(&attr);
	attr.policy = policy;
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.schedparam_ex = param;
	attr.fp = 1;
	attr.name = p->comm;

	ret = pthread_create(&k_tid, &attr, NULL, NULL);
	if (ret)
		return -ret;

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
	cobalt_thread_abort(k_tid, NULL);

	return ret;
}

#define __pthread_detach  __cobalt_call_not_available

static pthread_t __pthread_shadow(struct task_struct *p,
				  struct cobalt_hkey *hkey,
				  unsigned long __user *u_mode_offset)
{
	pthread_attr_t attr;
	pthread_t k_tid;
	pid_t h_tid;
	int err;

	pthread_attr_init(&attr);
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.name = p->comm;

	err = pthread_create(&k_tid, &attr, NULL, NULL);

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
		cobalt_thread_abort(k_tid, NULL);
	else
		k_tid->hkey = *hkey;

	return err ? ERR_PTR(err) : k_tid;
}

static int __pthread_setschedparam(unsigned long tid,
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
		k_tid = __pthread_shadow(current, &hkey, u_mode_offset);
		if (IS_ERR(k_tid))
			return PTR_ERR(k_tid);

		promoted = 1;
	}
	if (k_tid)
		err = -pthread_setschedparam(k_tid, policy, &param);
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

static int __pthread_setschedparam_ex(unsigned long tid,
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
		k_tid = __pthread_shadow(current, &hkey, u_mode_offset);
		if (IS_ERR(k_tid))
			return PTR_ERR(k_tid);

		promoted = 1;
	}
	if (k_tid)
		err = -pthread_setschedparam_ex(k_tid, policy, &param);
	else
		err = -EPERM;

	if (err == 0 &&
	    __xn_safe_copy_to_user(u_promoted, &promoted, sizeof(promoted)))
		err = -EFAULT;

	return err;
}

static int __pthread_getschedparam(unsigned long tid,
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

	err = -pthread_getschedparam(k_tid, &policy, &param);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_policy, &policy, sizeof(int)))
		return -EFAULT;

	return __xn_safe_copy_to_user(u_param, &param, sizeof(param));
}

static int __pthread_getschedparam_ex(unsigned long tid,
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

	err = -pthread_getschedparam_ex(k_tid, &policy, &param);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_policy, &policy, sizeof(int)))
		return -EFAULT;

	return __xn_safe_copy_to_user(u_param, &param, sizeof(param));
}

static int __sched_yield(void)
{
	pthread_t thread = thread2pthread(xnshadow_thread(current));
	struct sched_param_ex param;
	int policy;

	pthread_getschedparam_ex(thread, &policy, &param);
	sched_yield();

	return policy == SCHED_OTHER;
}

static int __pthread_make_periodic_np(unsigned long tid,
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

	return -pthread_make_periodic_np(k_tid, clk_id, &startt, &periodt);
}

static int __pthread_wait_np(unsigned long __user *u_overruns)
{
	unsigned long overruns;
	int err;

	err = -pthread_wait_np(&overruns);

	if (u_overruns && (err == 0 || err == -ETIMEDOUT))
		if (__xn_safe_copy_to_user(u_overruns,
					   &overruns, sizeof(overruns)))
			err = -EFAULT;
	return err;
}

static int __pthread_set_mode_np(int clrmask, int setmask, int __user *u_mode_r)
{
	int ret, old;

	ret = -pthread_set_mode_np(clrmask, setmask, &old);
	if (ret)
		return ret;

	if (u_mode_r && __xn_safe_copy_to_user(u_mode_r, &old, sizeof(old)))
		return -EFAULT;

	return 0;
}

static int __pthread_set_name_np(unsigned long tid,
				 const char __user *u_name)
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

	return -pthread_set_name_np(k_tid, name);
}

static int __pthread_probe_np(pid_t h_tid)
{
	return cobalt_thread_probe(h_tid);
}

static int __pthread_kill(unsigned long tid, int sig)
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
	 * __pthread_kill syscall as __xn_exec_primary would be
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

	return -pthread_kill(k_tid, sig);
}

static int __pthread_stat(unsigned long tid,
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

static int __clock_getres(clockid_t clock_id,
			  struct timespec __user *u_ts)
{
	struct timespec ts;
	int err;

	err = clock_getres(clock_id, &ts);
	if (err == 0 && __xn_safe_copy_to_user(u_ts, &ts, sizeof(ts)))
		return -EFAULT;

	return err ? -thread_get_errno() : 0;
}

static int __clock_gettime(clockid_t clock_id,
			   struct timespec __user *u_ts)
{
	struct timespec ts;
	int err;

	err = clock_gettime(clock_id, &ts);
	if (err == 0 && __xn_safe_copy_to_user(u_ts, &ts, sizeof(ts)))
		return -EFAULT;

	return err ? -thread_get_errno() : 0;
}

static int __clock_settime(clockid_t clock_id,
			   const struct timespec __user *u_ts)
{
	struct timespec ts;

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	return clock_settime(clock_id, &ts) ? -thread_get_errno() : 0;
}

static int __clock_nanosleep(clockid_t clock_id,
			     int flags,
			     const struct timespec __user *u_rqt,
			     struct timespec __user *u_rmt)
{
	struct timespec rqt, rmt, *rmtp = NULL;
	int err;

	if (u_rmt)
		rmtp = &rmt;

	if (__xn_safe_copy_from_user(&rqt, u_rqt, sizeof(rqt)))
		return -EFAULT;

	err = clock_nanosleep(clock_id, flags, &rqt, rmtp);
	if (err != EINTR)
		return -err;

	if (rmtp && __xn_safe_copy_to_user(u_rmt, rmtp, sizeof(*rmtp)))
		return -EFAULT;

	return -EINTR;
}

static int __pthread_mutexattr_init(pthread_mutexattr_t __user *u_attr)
{
	pthread_mutexattr_t attr;
	int err;

	err = pthread_mutexattr_init(&attr);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_mutexattr_destroy(pthread_mutexattr_t __user *u_attr)
{
	pthread_mutexattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_destroy(&attr);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_mutexattr_gettype(const pthread_mutexattr_t __user *u_attr,
				       int __user *u_type)
{
	pthread_mutexattr_t attr;
	int err, type;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_gettype(&attr, &type);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_type, &type, sizeof(*u_type));
}

static int __pthread_mutexattr_settype(pthread_mutexattr_t __user *u_attr,
				       int type)
{
	pthread_mutexattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_settype(&attr, type);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_mutexattr_getprotocol(const pthread_mutexattr_t __user *u_attr,
					   int __user *u_proto)
{
	pthread_mutexattr_t attr;
	int err, proto;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_getprotocol(&attr, &proto);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_proto, &proto, sizeof(*u_proto));
}

static int __pthread_mutexattr_setprotocol(pthread_mutexattr_t __user *u_attr,
					   int proto)
{
	pthread_mutexattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_setprotocol(&attr, proto);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_mutexattr_getpshared(const pthread_mutexattr_t __user *u_attr,
					   int __user *u_pshared)
{
	pthread_mutexattr_t attr;
	int err, pshared;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_getpshared(&attr, &pshared);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_pshared, &pshared, sizeof(*u_pshared));
}

static int __pthread_mutexattr_setpshared(pthread_mutexattr_t __user *u_attr,
					  int pshared)
{
	pthread_mutexattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_mutexattr_setpshared(&attr, pshared);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_condattr_init(pthread_condattr_t __user *u_attr)
{
	pthread_condattr_t attr;
	int err;

	err = pthread_condattr_init(&attr);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_condattr_destroy(pthread_condattr_t __user *u_attr)
{
	pthread_condattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_condattr_destroy(&attr);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_condattr_getclock(const pthread_condattr_t __user *u_attr,
				       clockid_t __user *u_clock)
{
	pthread_condattr_t attr;
	clockid_t clock;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_condattr_getclock(&attr, &clock);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_clock, &clock, sizeof(*u_clock));
}

static int __pthread_condattr_setclock(pthread_condattr_t __user *u_attr,
				       clockid_t clock)
{
	pthread_condattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_condattr_setclock(&attr, clock);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

static int __pthread_condattr_getpshared(const pthread_condattr_t __user *u_attr,
					 int __user *u_pshared)
{
	pthread_condattr_t attr;
	int err, pshared;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_condattr_getpshared(&attr, &pshared);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_pshared, &pshared, sizeof(*u_pshared));
}

static int __pthread_condattr_setpshared(pthread_condattr_t __user *u_attr,
					 int pshared)
{
	pthread_condattr_t attr;
	int err;

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = pthread_condattr_setpshared(&attr, pshared);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(*u_attr));
}

/* mq_open(name, oflags, mode, attr, ufd) */
static int __mq_open(const char __user *u_name,
		     int oflags,
		     mode_t mode,
		     struct mq_attr __user *u_attr,
		     mqd_t uqd)
{
	struct mq_attr locattr, *attr;
	char name[COBALT_MAXNAME];
	cobalt_ufd_t *assoc;
	cobalt_queues_t *q;
	unsigned len;
	mqd_t kqd;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return -EFAULT;

	if (len >= sizeof(name))
		return -ENAMETOOLONG;
	if (len == 0)
		return -EINVAL;

	if ((oflags & O_CREAT) && u_attr) {
		if (__xn_safe_copy_from_user(&locattr, u_attr, sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = NULL;

	kqd = mq_open(name, oflags, mode, attr);
	if (kqd == -1)
		return -thread_get_errno();

	assoc = xnmalloc(sizeof(*assoc));
	if (assoc == NULL) {
		mq_close(kqd);
		return -ENOSPC;
	}

	assoc->kfd = kqd;

	err = cobalt_assoc_insert(&q->uqds, &assoc->assoc, (u_long)uqd);
	if (err) {
		xnfree(assoc);
		mq_close(kqd);
	}

	return err;
}

static int __mq_close(mqd_t uqd)
{
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_remove(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	err = mq_close(assoc2ufd(assoc)->kfd);
	xnfree(assoc2ufd(assoc));

	return !err ? 0 : -thread_get_errno();
}

static int __mq_unlink(const char __user *u_name)
{
	char name[COBALT_MAXNAME];
	unsigned len;
	int err;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return -EFAULT;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	err = mq_unlink(name);

	return err ? -thread_get_errno() : 0;
}

static int __mq_getattr(mqd_t uqd,
			struct mq_attr __user *u_attr)
{
	cobalt_assoc_t *assoc;
	struct mq_attr attr;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	err = mq_getattr(ufd->kfd, &attr);
	if (err)
		return -thread_get_errno();

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(attr));
}

static int __mq_setattr(mqd_t uqd,
			const struct mq_attr __user *u_attr,
			struct mq_attr __user *u_oattr)
{
	struct mq_attr attr, oattr;
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = mq_setattr(ufd->kfd, &attr, &oattr);
	if (err)
		return -thread_get_errno();

	if (u_oattr)
		return __xn_safe_copy_to_user(u_oattr, &oattr, sizeof(oattr));

	return 0;
}

static int __mq_send(mqd_t uqd,
		     const void __user *u_buf,
		     size_t len,
		     unsigned int prio)
{
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	cobalt_msg_t *msg;
	cobalt_ufd_t *ufd;
	cobalt_mq_t *mq;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (len > 0 && !access_rok(u_buf, len))
		return -EFAULT;

	msg = cobalt_mq_timedsend_inner(&mq, ufd->kfd, len, NULL);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_from_user(msg->data, u_buf, len)) {
		cobalt_mq_finish_send(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	msg->len = len;
	cobalt_msg_set_prio(msg, prio);

	return cobalt_mq_finish_send(ufd->kfd, mq, msg);
}

static int __mq_timedsend(mqd_t uqd,
			  const void __user *u_buf,
			  size_t len,
			  unsigned int prio,
			  const struct timespec __user *u_ts)
{
	struct timespec timeout, *timeoutp;
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	cobalt_msg_t *msg;
	cobalt_ufd_t *ufd;
	cobalt_mq_t *mq;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (len > 0 && !access_rok(u_buf, len))
		return -EFAULT;

	if (u_ts) {
		if (__xn_safe_copy_from_user(&timeout, u_ts, sizeof(timeout)))
			return -EFAULT;
		timeoutp = &timeout;
	} else
		timeoutp = NULL;

	msg = cobalt_mq_timedsend_inner(&mq, ufd->kfd, len, timeoutp);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if(__xn_copy_from_user(msg->data, u_buf, len)) {
		cobalt_mq_finish_send(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	msg->len = len;
	cobalt_msg_set_prio(msg, prio);

	return cobalt_mq_finish_send(ufd->kfd, mq, msg);
}

static int __mq_receive(mqd_t uqd,
			void __user *u_buf,
			ssize_t __user *u_len,
			unsigned int __user *u_prio)
{
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;
	cobalt_msg_t *msg;
	cobalt_mq_t *mq;
	unsigned prio;
	ssize_t len;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&len, u_len, sizeof(len)))
		return -EFAULT;

	if (u_prio && !access_wok(u_prio, sizeof(prio)))
		return -EFAULT;

	if (len > 0 && !access_wok(u_buf, len))
		return -EFAULT;

	msg = cobalt_mq_timedrcv_inner(&mq, ufd->kfd, len, NULL);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_to_user(u_buf, msg->data, msg->len)) {
		cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	len = msg->len;
	prio = cobalt_msg_get_prio(msg);

	err = cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_len, &len, sizeof(len)))
		return -EFAULT;

	if (u_prio &&
	    __xn_safe_copy_to_user(u_prio, &prio, sizeof(prio)))
		return -EFAULT;

	return 0;
}

static int __mq_timedreceive(mqd_t uqd,
			     void __user *u_buf,
			     ssize_t __user *u_len,
			     unsigned int __user *u_prio,
			     const struct timespec __user *u_ts)
{
	struct timespec timeout, *timeoutp;
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	unsigned int prio;
	cobalt_ufd_t *ufd;
	cobalt_msg_t *msg;
	cobalt_mq_t *mq;
	ssize_t len;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&len, u_len, sizeof(len)))
		return -EFAULT;

	if (len > 0 && !access_wok(u_buf, len))
		return -EFAULT;

	if (u_ts) {
		if (__xn_safe_copy_from_user(&timeout, u_ts, sizeof(timeout)))
			return -EFAULT;

		timeoutp = &timeout;
	} else
		timeoutp = NULL;

	msg = cobalt_mq_timedrcv_inner(&mq, ufd->kfd, len, timeoutp);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_to_user(u_buf, msg->data, msg->len)) {
		cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	len = msg->len;
	prio = cobalt_msg_get_prio(msg);

	err = cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_len, &len, sizeof(len)))
		return -EFAULT;

	if (u_prio && __xn_safe_copy_to_user(u_prio, &prio, sizeof(prio)))
		return -EFAULT;

	return 0;
}

static int __mq_notify(mqd_t uqd,
		       const struct sigevent __user *u_sev)
{
	cobalt_assoc_t *assoc;
	struct sigevent sev;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&sev, u_sev, sizeof(sev)))
		return -EFAULT;

	if (mq_notify(ufd->kfd, &sev))
		return -thread_get_errno();

	return 0;
}

static int __timer_create(clockid_t clock,
			  const struct sigevent __user *u_sev,
			  timer_t __user *u_tm)
{
	union __xeno_sem sm, __user *u_sem;
	struct sigevent sev, *evp = &sev;
	timer_t tm;
	int ret;

	if (u_sev) {
		if (__xn_safe_copy_from_user(&sev, u_sev, sizeof(sev)))
			return -EFAULT;

		if (sev.sigev_notify == SIGEV_THREAD_ID) {
			u_sem = sev.sigev_value.sival_ptr;

			if (__xn_safe_copy_from_user(&sm, u_sem, sizeof(sm)))
				return -EFAULT;

			sev.sigev_value.sival_ptr = &sm.native_sem;
		}
	} else
		evp = NULL;

	ret = timer_create(clock, evp, &tm);
	if (ret)
		return -thread_get_errno();

	if (__xn_safe_copy_to_user(u_tm, &tm, sizeof(tm))) {
		timer_delete(tm);
		return -EFAULT;
	}

	return 0;
}

static int __timer_delete(timer_t tm)
{
	int ret = timer_delete(tm);
	return ret == 0 ? 0 : -thread_get_errno();
}

static int __timer_settime(timer_t tm,
			   int flags,
			   const struct itimerspec __user *u_newval,
			   struct itimerspec __user *u_oldval)
{
	struct itimerspec newv, oldv, *oldvp;
	int ret;

	oldvp = u_oldval == 0 ? NULL : &oldv;

	if (__xn_safe_copy_from_user(&newv, u_newval, sizeof(newv)))
		return -EFAULT;

	ret = timer_settime(tm, flags, &newv, oldvp);
	if (ret)
		return -thread_get_errno();

	if (oldvp && __xn_safe_copy_to_user(u_oldval, oldvp, sizeof(oldv))) {
		timer_settime(tm, flags, oldvp, NULL);
		return -EFAULT;
	}

	return 0;
}

static int __timer_gettime(timer_t tm,
			   struct itimerspec __user *u_val)
{
	struct itimerspec val;
	int ret;

	ret = timer_gettime(tm, &val);
	if (ret)
		return -thread_get_errno();

	return __xn_safe_copy_to_user(u_val, &val, sizeof(val));
}

static int __timer_getoverrun(timer_t tm)
{
	int ret = timer_getoverrun(tm);
	return ret >= 0 ? ret : -thread_get_errno();
}

static int fd_valid_p(int fd)
{
	cobalt_queues_t *q;
	const int rtdm_fd_start = FD_SETSIZE - RTDM_FD_MAX;

	if (fd >= rtdm_fd_start) {
		struct rtdm_dev_context *ctx;
		ctx = rtdm_context_get(fd - rtdm_fd_start);
		if (ctx) {
			rtdm_context_unlock(ctx);
			return 1;
		}
		return 0;
	}

	q = cobalt_queues();
	if (q == NULL)
		return 0;

	return cobalt_assoc_lookup(&q->uqds, fd) != NULL;
}

static int first_fd_valid_p(fd_set *fds[XNSELECT_MAX_TYPES], int nfds)
{
	int i, fd;

	for (i = 0; i < XNSELECT_MAX_TYPES; i++)
		if (fds[i]
		    && (fd = find_first_bit(fds[i]->fds_bits, nfds)) < nfds)
			return fd_valid_p(fd);

	/* All empty is correct, used as a "sleep" mechanism by strange
	   applications. */
	return 1;
}

static int select_bind_one(struct xnselector *selector, unsigned type, int fd)
{
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	const int rtdm_fd_start = FD_SETSIZE - RTDM_FD_MAX;

	if (fd >= rtdm_fd_start)
		return rtdm_select_bind(fd - rtdm_fd_start,
					selector, type, fd);

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->uqds, fd);
	if (assoc == NULL)
		return -EBADF;

	return cobalt_mq_select_bind(assoc2ufd(assoc)->kfd, selector, type, fd);
}

static int select_bind_all(struct xnselector *selector,
			   fd_set *fds[XNSELECT_MAX_TYPES], int nfds)
{
	unsigned fd, type;
	int err;

	for (type = 0; type < XNSELECT_MAX_TYPES; type++) {
		fd_set *set = fds[type];
		if (set)
			for (fd = find_first_bit(set->fds_bits, nfds);
			     fd < nfds;
			     fd = find_next_bit(set->fds_bits, nfds, fd + 1)) {
				err = select_bind_one(selector, type, fd);
				if (err)
					return err;
			}
	}

	return 0;
}

/* int select(int, fd_set *, fd_set *, fd_set *, struct timeval *) */
static int __select(int nfds,
		    fd_set __user *u_rfds,
		    fd_set __user *u_wfds,
		    fd_set __user *u_xfds,
		    struct timeval __user *u_tv)
{
	fd_set __user *ufd_sets[XNSELECT_MAX_TYPES] = {
		[XNSELECT_READ] = u_rfds,
		[XNSELECT_WRITE] = u_wfds,
		[XNSELECT_EXCEPT] = u_xfds
	};
	fd_set *in_fds[XNSELECT_MAX_TYPES] = {NULL, NULL, NULL};
	fd_set *out_fds[XNSELECT_MAX_TYPES] = {NULL, NULL, NULL};
	fd_set in_fds_storage[XNSELECT_MAX_TYPES],
		out_fds_storage[XNSELECT_MAX_TYPES];
	xnticks_t timeout = XN_INFINITE;
	xntmode_t mode = XN_RELATIVE;
	struct xnselector *selector;
	struct timeval tv;
	xnthread_t *thread;
	size_t fds_size;
	int i, err;

	thread = xnpod_current_thread();
	if (!thread)
		return -EPERM;

	if (u_tv) {
		if (!access_wok(u_tv, sizeof(tv))
		    || __xn_copy_from_user(&tv, u_tv, sizeof(tv)))
			return -EFAULT;

		if (tv.tv_usec > 1000000)
			return -EINVAL;

		timeout = clock_get_ticks(CLOCK_MONOTONIC) + tv2ns(&tv);
		mode = XN_ABSOLUTE;
	}

	fds_size = __FDELT(nfds + __NFDBITS - 1) * sizeof(long);

	for (i = 0; i < XNSELECT_MAX_TYPES; i++)
		if (ufd_sets[i]) {
			in_fds[i] = &in_fds_storage[i];
			out_fds[i] = & out_fds_storage[i];
			if (!access_wok((void __user *) ufd_sets[i],
					sizeof(fd_set))
			    || __xn_copy_from_user(in_fds[i],
						   (void __user *) ufd_sets[i],
						   fds_size))
				return -EFAULT;
		}

	selector = thread->selector;
	if (!selector) {
		/* This function may be called from pure Linux fd_sets, we want
		   to avoid the xnselector allocation in this case, so, we do a
		   simple test: test if the first file descriptor we find in the
		   fd_set is an RTDM descriptor or a message queue descriptor. */
		if (!first_fd_valid_p(in_fds, nfds))
			return -EBADF;

		if (!(selector = xnmalloc(sizeof(*thread->selector))))
			return -ENOMEM;
		xnselector_init(selector);
		thread->selector = selector;

		/* Bind directly the file descriptors, we do not need to go
		   through xnselect returning -ECHRNG */
		if ((err = select_bind_all(selector, in_fds, nfds)))
			return err;
	}

	do {
		err = xnselect(selector, out_fds, in_fds, nfds, timeout, mode);

		if (err == -ECHRNG) {
			int err = select_bind_all(selector, out_fds, nfds);
			if (err)
				return err;
		}
	} while (err == -ECHRNG);

	if (u_tv && (err > 0 || err == -EINTR)) {
		xnsticks_t diff = timeout - clock_get_ticks(CLOCK_MONOTONIC);
		if (diff > 0)
			ticks2tv(&tv, diff);
		else
			tv.tv_sec = tv.tv_usec = 0;

		if (__xn_copy_to_user(u_tv, &tv, sizeof(tv)))
			return -EFAULT;
	}

	if (err > 0)
		for (i = 0; i < XNSELECT_MAX_TYPES; i++)
			if (ufd_sets[i]
			    && __xn_copy_to_user((void __user *) ufd_sets[i],
						 out_fds[i], sizeof(fd_set)))
				return -EFAULT;
	return err;
}

static int __sched_min_prio(int policy)
{
	int ret = sched_get_priority_min(policy);
	return ret >= 0 ? ret : -thread_get_errno();
}

static int __sched_max_prio(int policy)
{
	int ret = sched_get_priority_max(policy);
	return ret >= 0 ? ret : -thread_get_errno();
}

#ifdef CONFIG_XENO_OPT_POSIX_SHM

static int __shm_open(const char __user *u_name,
		      int oflag,
		      mode_t mode,
		      int fd)
{
	char name[COBALT_MAXNAME];
	cobalt_ufd_t *assoc;
	cobalt_queues_t *q;
	int kfd, err, len;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return -EFAULT;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;
	if (len == 0)
		return -EINVAL;

	kfd = shm_open(name, oflag, mode);
	if (kfd == -1)
		return -thread_get_errno();

	assoc = xnmalloc(sizeof(*assoc));
	if (assoc == NULL) {
		cobalt_shm_close(kfd);
		return -ENOSPC;
	}

	assoc->kfd = kfd;

	err = cobalt_assoc_insert(&q->ufds, &assoc->assoc, fd);
	if (err) {
		xnfree(assoc);
		close(kfd);
	}

	return err;
}

static int __shm_unlink(const char __user *u_name)
{
	char name[COBALT_MAXNAME];
	unsigned len;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return -EFAULT;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	return shm_unlink(name) == 0 ? 0 : -thread_get_errno();
}

static int __shm_close(int fd)
{
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_remove(&q->ufds, fd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	err = close(ufd->kfd);
	xnfree(ufd);

	return err == 0 ? 0 : -thread_get_errno();
}

static int __ftruncate(int fd, off_t len)
{
	cobalt_assoc_t *assoc;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->ufds, fd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	err = ftruncate(ufd->kfd, len);

	return err == 0 ? 0 : -thread_get_errno();
}

typedef struct {
	void *kaddr;
	unsigned long len;
	xnheap_t *ioctl_cookie;
	unsigned long heapsize;
	unsigned long offset;
} cobalt_mmap_param_t;

static int __mmap_prologue(size_t len,
			   int fd,
			   off_t off,
			   cobalt_mmap_param_t __user *u_param)
{
	cobalt_mmap_param_t mmap_param;
	cobalt_assoc_t *assoc;
	struct xnheap *heap;
	cobalt_queues_t *q;
	cobalt_ufd_t *ufd;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->ufds, fd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	/*
	 * We do not care for the real flags and protection, this
	 * mapping is a placeholder.
	 */
	mmap_param.kaddr = mmap(NULL,
				len,
				PROT_READ,
				MAP_SHARED, ufd->kfd, off);

	if (mmap_param.kaddr == MAP_FAILED)
		return -thread_get_errno();

	if ((err =
	     cobalt_xnheap_get(&mmap_param.ioctl_cookie, mmap_param.kaddr))) {
		munmap(mmap_param.kaddr, len);
		return err;
	}

	heap = mmap_param.ioctl_cookie;
	mmap_param.len = len;
	mmap_param.heapsize = xnheap_extentsize(heap);
	mmap_param.offset = xnheap_mapped_offset(heap, mmap_param.kaddr);
	mmap_param.offset += xnheap_base_memory(heap);

	return __xn_safe_copy_to_user(u_param, &mmap_param,
				      sizeof(mmap_param));
}

static int __mmap_epilogue(void __user *u_addr,
			   cobalt_mmap_param_t __user *u_param)
{
	cobalt_mmap_param_t mmap_param;
	cobalt_umap_t *umap;
	int err;

	if (__xn_safe_copy_from_user(&mmap_param, u_param,
				     sizeof(mmap_param)))
		return -EFAULT;

	if (u_addr == MAP_FAILED) {
		munmap(mmap_param.kaddr, mmap_param.len);
		return 0;
	}

	umap = xnmalloc(sizeof(*umap));
	if (umap == NULL) {
		munmap(mmap_param.kaddr, mmap_param.len);
		return -EAGAIN;
	}

	umap->kaddr = mmap_param.kaddr;
	umap->len = mmap_param.len;

	err = cobalt_assoc_insert(&cobalt_queues()->umaps,
				 &umap->assoc, (u_long)u_addr);
	if (err)
		munmap(mmap_param.kaddr, mmap_param.len);

	return err;
}

struct  __uunmap_struct {
	unsigned long mapsize;
	unsigned long offset;
};

/* munmap_prologue(uaddr, len, &unmap) */
static int __munmap_prologue(void __user *u_addr,
			     size_t len,
			     struct __uunmap_struct __user *u_unmap)
{
	struct  __uunmap_struct uunmap;
	cobalt_assoc_t *assoc;
	cobalt_umap_t *umap;
	cobalt_queues_t *q;
	xnheap_t *heap;
	int err;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&q->umaps, (u_long)u_addr);
	if (assoc == NULL)
		return -EBADF;

	umap = assoc2umap(assoc);

	err = cobalt_xnheap_get(&heap, umap->kaddr);
	if (err)
		return err;

	uunmap.mapsize = xnheap_extentsize(heap);
	uunmap.offset = xnheap_mapped_offset(heap, umap->kaddr);

	return __xn_safe_copy_to_user(u_unmap, &uunmap, sizeof(uunmap));
}

static int __munmap_epilogue(void __user *u_addr,
			     size_t len)
{
	cobalt_assoc_t *assoc;
	cobalt_umap_t *umap;
	spl_t s;
	int err;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);

	assoc = cobalt_assoc_lookup(&cobalt_queues()->umaps, (u_long)u_addr);
	if (assoc == NULL) {
		xnlock_put_irqrestore(&cobalt_assoc_lock, s);
		return -EBADF;
	}

	umap = assoc2umap(assoc);

	if (umap->len != len) {
		xnlock_put_irqrestore(&cobalt_assoc_lock, s);
		return -EINVAL;
	}

	cobalt_assoc_remove(&cobalt_queues()->umaps, (u_long)u_addr);
	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

	err = munmap(umap->kaddr, len);
	if (err == 0)
		xnfree(umap);

	return err == 0 ? 0 : -thread_get_errno();
}
#else /* !CONFIG_XENO_OPT_POSIX_SHM */

#define __shm_open        __cobalt_call_not_available
#define __shm_unlink      __cobalt_call_not_available
#define __shm_close       __cobalt_call_not_available
#define __ftruncate       __cobalt_call_not_available
#define __mmap_prologue   __cobalt_call_not_available
#define __mmap_epilogue   __cobalt_call_not_available
#define __munmap_prologue __cobalt_call_not_available
#define __munmap_epilogue __cobalt_call_not_available

#endif /* !CONFIG_XENO_OPT_POSIX_SHM */

int __cobalt_call_not_available(void)
{
	return -ENOSYS;
}

static struct xnsysent __systab[] = {
	SKINCALL_DEF(sc_cobalt_thread_create, __pthread_create, init),
	SKINCALL_DEF(sc_cobalt_thread_detach, __pthread_detach, any),
	SKINCALL_DEF(sc_cobalt_thread_setschedparam, __pthread_setschedparam, conforming),
	SKINCALL_DEF(sc_cobalt_thread_setschedparam_ex, __pthread_setschedparam_ex, conforming),
	SKINCALL_DEF(sc_cobalt_thread_getschedparam, __pthread_getschedparam, any),
	SKINCALL_DEF(sc_cobalt_thread_getschedparam_ex, __pthread_getschedparam_ex, any),
	SKINCALL_DEF(sc_cobalt_sched_yield, __sched_yield, primary),
	SKINCALL_DEF(sc_cobalt_thread_make_periodic, __pthread_make_periodic_np, conforming),
	SKINCALL_DEF(sc_cobalt_thread_wait, __pthread_wait_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_mode, __pthread_set_mode_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_name, __pthread_set_name_np, any),
	SKINCALL_DEF(sc_cobalt_thread_probe, __pthread_probe_np, any),
	SKINCALL_DEF(sc_cobalt_thread_kill, __pthread_kill, any),
	SKINCALL_DEF(sc_cobalt_thread_getstat, __pthread_stat, any),
	SKINCALL_DEF(sc_cobalt_sem_init, cobalt_sem_init, any),
	SKINCALL_DEF(sc_cobalt_sem_destroy, cobalt_sem_destroy, any),
	SKINCALL_DEF(sc_cobalt_sem_post, cobalt_sem_post, any),
	SKINCALL_DEF(sc_cobalt_sem_wait, cobalt_sem_wait, primary),
	SKINCALL_DEF(sc_cobalt_sem_timedwait, cobalt_sem_timedwait, primary),
	SKINCALL_DEF(sc_cobalt_sem_trywait, cobalt_sem_trywait, primary),
	SKINCALL_DEF(sc_cobalt_sem_getvalue, cobalt_sem_getvalue, any),
	SKINCALL_DEF(sc_cobalt_sem_open, cobalt_sem_open, any),
	SKINCALL_DEF(sc_cobalt_sem_close, cobalt_sem_close, any),
	SKINCALL_DEF(sc_cobalt_sem_unlink, cobalt_sem_unlink, any),
	SKINCALL_DEF(sc_cobalt_sem_init_np, cobalt_sem_init_np, any),
	SKINCALL_DEF(sc_cobalt_sem_broadcast_np, cobalt_sem_broadcast_np, any),
	SKINCALL_DEF(sc_cobalt_clock_getres, __clock_getres, any),
	SKINCALL_DEF(sc_cobalt_clock_gettime, __clock_gettime, any),
	SKINCALL_DEF(sc_cobalt_clock_settime, __clock_settime, any),
	SKINCALL_DEF(sc_cobalt_clock_nanosleep, __clock_nanosleep, nonrestartable),
	SKINCALL_DEF(sc_cobalt_mutex_init, cobalt_mutex_init, any),
	SKINCALL_DEF(sc_cobalt_check_init, cobalt_mutex_check_init, any),
	SKINCALL_DEF(sc_cobalt_mutex_destroy, cobalt_mutex_destroy, any),
	SKINCALL_DEF(sc_cobalt_mutex_lock, cobalt_mutex_lock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_timedlock, cobalt_mutex_timedlock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_trylock, cobalt_mutex_trylock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_unlock, cobalt_mutex_unlock, nonrestartable),
	SKINCALL_DEF(sc_cobalt_cond_init, cobalt_cond_init, any),
	SKINCALL_DEF(sc_cobalt_cond_destroy, cobalt_cond_destroy, any),
	SKINCALL_DEF(sc_cobalt_cond_wait_prologue, cobalt_cond_wait_prologue, nonrestartable),
	SKINCALL_DEF(sc_cobalt_cond_wait_epilogue, cobalt_cond_wait_epilogue, primary),

	SKINCALL_DEF(sc_cobalt_mq_open, __mq_open, lostage),
	SKINCALL_DEF(sc_cobalt_mq_close, __mq_close, lostage),
	SKINCALL_DEF(sc_cobalt_mq_unlink, __mq_unlink, lostage),
	SKINCALL_DEF(sc_cobalt_mq_getattr, __mq_getattr, any),
	SKINCALL_DEF(sc_cobalt_mq_setattr, __mq_setattr, any),
	SKINCALL_DEF(sc_cobalt_mq_send, __mq_send, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedsend, __mq_timedsend, primary),
	SKINCALL_DEF(sc_cobalt_mq_receive, __mq_receive, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedreceive, __mq_timedreceive, primary),
	SKINCALL_DEF(sc_cobalt_mq_notify, __mq_notify, primary),
	SKINCALL_DEF(sc_cobalt_timer_create, __timer_create, any),
	SKINCALL_DEF(sc_cobalt_timer_delete, __timer_delete, any),
	SKINCALL_DEF(sc_cobalt_timer_settime, __timer_settime, primary),
	SKINCALL_DEF(sc_cobalt_timer_gettime, __timer_gettime, any),
	SKINCALL_DEF(sc_cobalt_timer_getoverrun, __timer_getoverrun, any),
	SKINCALL_DEF(sc_cobalt_shm_open, __shm_open, lostage),
	SKINCALL_DEF(sc_cobalt_shm_unlink, __shm_unlink, lostage),
	SKINCALL_DEF(sc_cobalt_shm_close, __shm_close, lostage),
	SKINCALL_DEF(sc_cobalt_ftruncate, __ftruncate, lostage),
	SKINCALL_DEF(sc_cobalt_mmap_prologue, __mmap_prologue, lostage),
	SKINCALL_DEF(sc_cobalt_mmap_epilogue, __mmap_epilogue, lostage),
	SKINCALL_DEF(sc_cobalt_munmap_prologue, __munmap_prologue, lostage),
	SKINCALL_DEF(sc_cobalt_munmap_epilogue, __munmap_epilogue, lostage),
	SKINCALL_DEF(sc_cobalt_mutexattr_init, __pthread_mutexattr_init, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_destroy, __pthread_mutexattr_destroy, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_gettype, __pthread_mutexattr_gettype, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_settype, __pthread_mutexattr_settype, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_getprotocol, __pthread_mutexattr_getprotocol, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_setprotocol, __pthread_mutexattr_setprotocol, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_getpshared, __pthread_mutexattr_getpshared, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_setpshared, __pthread_mutexattr_setpshared, any),
	SKINCALL_DEF(sc_cobalt_condattr_init, __pthread_condattr_init, any),
	SKINCALL_DEF(sc_cobalt_condattr_destroy, __pthread_condattr_destroy, any),
	SKINCALL_DEF(sc_cobalt_condattr_getclock, __pthread_condattr_getclock, any),
	SKINCALL_DEF(sc_cobalt_condattr_setclock, __pthread_condattr_setclock, any),
	SKINCALL_DEF(sc_cobalt_condattr_getpshared, __pthread_condattr_getpshared, any),
	SKINCALL_DEF(sc_cobalt_condattr_setpshared, __pthread_condattr_setpshared, any),
	SKINCALL_DEF(sc_cobalt_select, __select, primary),
	SKINCALL_DEF(sc_cobalt_sched_minprio, __sched_min_prio, any),
	SKINCALL_DEF(sc_cobalt_sched_maxprio, __sched_max_prio, any),
	SKINCALL_DEF(sc_cobalt_monitor_init, cobalt_monitor_init, any),
	SKINCALL_DEF(sc_cobalt_monitor_destroy, cobalt_monitor_destroy, any),
	SKINCALL_DEF(sc_cobalt_monitor_enter, cobalt_monitor_enter, primary),
	SKINCALL_DEF(sc_cobalt_monitor_wait, cobalt_monitor_wait, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_sync, cobalt_monitor_sync, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_exit, cobalt_monitor_exit, nonrestartable),
};

static void __shadow_delete_hook(xnthread_t *thread)
{
	if (xnthread_get_magic(thread) == COBALT_SKIN_MAGIC &&
	    xnthread_test_state(thread, XNSHADOW)) {
		pthread_t k_tid = thread2pthread(thread);
		cobalt_thread_unhash(&k_tid->hkey);
		if (xnthread_test_state(thread, XNMAPPED))
			xnshadow_unmap(thread);
	}
}

static void *cobalt_eventcb(int event, void *data)
{
	cobalt_queues_t *q;

	switch (event) {
	case XNSHADOW_CLIENT_ATTACH:
		q = (cobalt_queues_t *) xnarch_alloc_host_mem(sizeof(*q));
		if (q == NULL)
			return ERR_PTR(-ENOSPC);

		initq(&q->kqueues.condq);
#ifdef CONFIG_XENO_OPT_POSIX_INTR
		initq(&q->kqueues.intrq);
#endif /* CONFIG_XENO_OPT_POSIX_INTR */
		initq(&q->kqueues.mutexq);
		initq(&q->kqueues.semq);
		initq(&q->kqueues.threadq);
		initq(&q->kqueues.timerq);
		initq(&q->kqueues.monitorq);
		cobalt_assocq_init(&q->uqds);
		cobalt_assocq_init(&q->usems);
#ifdef CONFIG_XENO_OPT_POSIX_SHM
		cobalt_assocq_init(&q->umaps);
		cobalt_assocq_init(&q->ufds);
#endif /* CONFIG_XENO_OPT_POSIX_SHM */

		return &q->ppd;

	case XNSHADOW_CLIENT_DETACH:
		q = ppd2queues((xnshadow_ppd_t *) data);

#ifdef CONFIG_XENO_OPT_POSIX_SHM
		cobalt_shm_ufds_cleanup(q);
		cobalt_shm_umaps_cleanup(q);
#endif /* CONFIG_XENO_OPT_POSIX_SHM */
		cobalt_sem_usems_cleanup(q);
		cobalt_mq_uqds_cleanup(q);
		cobalt_monitorq_cleanup(&q->kqueues);
		cobalt_timerq_cleanup(&q->kqueues);
		cobalt_semq_cleanup(&q->kqueues);
		cobalt_mutexq_cleanup(&q->kqueues);
#ifdef CONFIG_XENO_OPT_POSIX_INTR
		cobalt_intrq_cleanup(&q->kqueues);
#endif /* CONFIG_XENO_OPT_POSIX_INTR */
		cobalt_condq_cleanup(&q->kqueues);

		xnarch_free_host_mem(q, sizeof(*q));

		return NULL;
	}

	return ERR_PTR(-EINVAL);
}

static struct xnskin_props __props = {
	.name = "posix",
	.magic = COBALT_SKIN_MAGIC,
	.nrcalls = sizeof(__systab) / sizeof(__systab[0]),
	.systab = __systab,
	.eventcb = &cobalt_eventcb,
};

int cobalt_syscall_init(void)
{
	cobalt_muxid = xnshadow_register_interface(&__props);

	if (cobalt_muxid < 0)
		return -ENOSYS;

	xnpod_add_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);

	return 0;
}

void cobalt_syscall_cleanup(void)
{
	xnpod_remove_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);
	xnshadow_unregister_interface(cobalt_muxid);
}
