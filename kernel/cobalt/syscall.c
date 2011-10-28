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
#include <linux/jhash.h>
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
#include "registry.h"	/* For PSE51_MAXNAME. */
#include "sem.h"
#include "shm.h"
#include "timer.h"
#include <rtdm/rtdm_driver.h>
#define RTDM_FD_MAX CONFIG_XENO_OPT_RTDM_FILDES

int pse51_muxid;

#define PTHREAD_HSLOTS (1 << 8)	/* Must be a power of 2 */

struct pthread_hash {
	pthread_t k_tid;	/* Xenomai in-kernel (nucleus) tid */
	pid_t h_tid;		/* Host (linux) tid */
	struct pse51_hkey hkey;
	struct pthread_hash *next;
};

struct tid_hash {
	pid_t tid;
	struct tid_hash *next;
};

static struct pthread_hash *pthread_table[PTHREAD_HSLOTS];

static struct tid_hash *tid_table[PTHREAD_HSLOTS];

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

static inline
struct pthread_hash *__pthread_hash(const struct pse51_hkey *hkey,
				    pthread_t k_tid,
				    pid_t h_tid)
{
	struct pthread_hash **pthead, *ptslot;
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

static inline void __pthread_unhash(const struct pse51_hkey *hkey)
{
	struct pthread_hash **pttail, *ptslot;
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

static pthread_t __pthread_find(const struct pse51_hkey *hkey)
{
	struct pthread_hash *ptslot;
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

static int __tid_probe(pid_t h_tid)
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

static int __pthread_create(unsigned long tid,
			    int policy, int sched_prio,
			    unsigned long __user *u_mode)
{
	struct task_struct *p = current;
	struct pse51_hkey hkey;
	pthread_attr_t attr;
	pthread_t k_tid;
	int err;

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
	attr.schedparam_ex.sched_priority = sched_prio;
	attr.fp = 1;
	attr.name = p->comm;

	err = pthread_create(&k_tid, &attr, NULL, NULL);

	if (err)
		return -err;	/* Conventionally, our error codes are negative. */

	err = xnshadow_map(&k_tid->threadbase, NULL, u_mode);
	if (err == 0 && !__pthread_hash(&hkey, k_tid, task_pid_vnr(p)))
		err = -ENOMEM;

	if (err)
		pse51_thread_abort(k_tid, NULL);
	else
		k_tid->hkey = hkey;

	return err;
}

#define __pthread_detach  __pse51_call_not_available

static pthread_t __pthread_shadow(struct task_struct *p,
				  struct pse51_hkey *hkey,
				  unsigned long __user *u_mode_offset)
{
	pthread_attr_t attr;
	pthread_t k_tid;
	int err;

	pthread_attr_init(&attr);
	attr.detachstate = PTHREAD_CREATE_DETACHED;
	attr.name = p->comm;

	err = pthread_create(&k_tid, &attr, NULL, NULL);

	if (err)
		return ERR_PTR(-err);

	err = xnshadow_map(&k_tid->threadbase, NULL, u_mode_offset);
	if (err == 0 && !__pthread_hash(hkey, k_tid, task_pid_vnr(p)))
		err = -EAGAIN;

	if (err)
		pse51_thread_abort(k_tid, NULL);
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
	struct pse51_hkey hkey;
	int err, promoted = 0;
	pthread_t k_tid;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

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
	struct pse51_hkey hkey;
	int err, promoted = 0;
	pthread_t k_tid;

	if (__xn_safe_copy_from_user(&param, u_param, sizeof(param)))
		return -EFAULT;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

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
	struct pse51_hkey hkey;
	pthread_t k_tid;
	int policy, err;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

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
	struct pse51_hkey hkey;
	pthread_t k_tid;
	int policy, err;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

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
	struct pse51_hkey hkey;
	pthread_t k_tid;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

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
	struct pse51_hkey hkey;
	pthread_t k_tid;

	if (__xn_safe_strncpy_from_user(name, u_name,
					sizeof(name) - 1) < 0)
		return -EFAULT;

	name[sizeof(name) - 1] = '\0';

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

	return -pthread_set_name_np(k_tid, name);
}

static int __pthread_probe_np(pid_t h_tid)
{
	return __tid_probe(h_tid);
}

static int __pthread_kill(unsigned long tid, int sig)
{
	struct pse51_hkey hkey;
	pthread_t k_tid;
	int ret;

	hkey.u_tid = tid;
	hkey.mm = current->mm;
	k_tid = __pthread_find(&hkey);

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

static int __sem_init(union __xeno_sem __user *u_sem,
		      int pshared, unsigned value)
{
	union __xeno_sem sm;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	if (sem_init(&sm.native_sem, pshared, value) == -1)
		return -thread_get_errno();

	return __xn_safe_copy_to_user(&u_sem->shadow_sem,
				      &sm.shadow_sem, sizeof(u_sem->shadow_sem));
}

static int __sem_post(union __xeno_sem __user *u_sem)
{
	union __xeno_sem sm;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	return sem_post(&sm.native_sem) == 0 ? 0 : -thread_get_errno();
}

static int __sem_wait(union __xeno_sem __user *u_sem)
{
	union __xeno_sem sm;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	return sem_wait(&sm.native_sem) == 0 ? 0 : -thread_get_errno();
}

static int __sem_timedwait(union __xeno_sem __user *u_sem,
			   struct timespec __user *u_ts)
{
	union __xeno_sem sm;
	struct timespec ts;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	return sem_timedwait(&sm.native_sem,
			     &ts) == 0 ? 0 : -thread_get_errno();
}

static int __sem_trywait(union __xeno_sem __user *u_sem)
{
	union __xeno_sem sm;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	return sem_trywait(&sm.native_sem) == 0 ? 0 : -thread_get_errno();
}

static int __sem_getvalue(union __xeno_sem __user *u_sem,
			  int __user *u_sval)
{
	union __xeno_sem sm;
	int err, sval;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	err = sem_getvalue(&sm.native_sem, &sval);
	if (err)
		return -thread_get_errno();

	return __xn_safe_copy_to_user(u_sval, &sval, sizeof(sval));
}

static int __sem_destroy(union __xeno_sem __user *u_sem)
{
	union __xeno_sem sm;
	int err;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     &u_sem->shadow_sem,
				     sizeof(sm.shadow_sem)))
		return -EFAULT;

	err = sem_destroy(&sm.native_sem);
	if (err)
		return -thread_get_errno();

	return __xn_safe_copy_to_user(&u_sem->shadow_sem,
				      &sm.shadow_sem, sizeof(u_sem->shadow_sem));
}

static int __sem_open(unsigned long __user *u_addr,
		      const char __user *u_name,
		      int oflags,
		      mode_t mode,
		      unsigned value)
{
	char name[PSE51_MAXNAME];
	union __xeno_sem *sm;
	pse51_assoc_t *assoc;
	unsigned long uaddr;
	pse51_queues_t *q;
	pse51_usem_t *usm;
	long len;
	int err;
	spl_t s;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	if (__xn_safe_copy_from_user(&uaddr, u_addr, sizeof(uaddr)))
		return -EFAULT;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return len;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;
	if (len == 0)
		return -EINVAL;

	if (!(oflags & O_CREAT))
		sm = (union __xeno_sem *)sem_open(name, oflags);
	else
		sm = (union __xeno_sem *)sem_open(name, oflags, mode, value);

	if (sm == SEM_FAILED)
		return -thread_get_errno();

	xnlock_get_irqsave(&pse51_assoc_lock, s);
	assoc = pse51_assoc_lookup(&q->usems, (u_long)sm->shadow_sem.sem);

	if (assoc) {
		usm = assoc2usem(assoc);
		++usm->refcnt;
		xnlock_put_irqrestore(&nklock, s);
		goto got_usm;
	}

	xnlock_put_irqrestore(&pse51_assoc_lock, s);

	usm = xnmalloc(sizeof(*usm));
	if (usm == NULL) {
		sem_close(&sm->native_sem);
		return -ENOSPC;
	}

	usm->uaddr = uaddr;
	usm->refcnt = 1;

	xnlock_get_irqsave(&pse51_assoc_lock, s);
	assoc = pse51_assoc_lookup(&q->usems, (u_long)sm->shadow_sem.sem);
	if (assoc) {
		assoc2usem(assoc)->refcnt++;
		xnlock_put_irqrestore(&nklock, s);
		xnfree(usm);
		usm = assoc2usem(assoc);
		goto got_usm;
	}

	pse51_assoc_insert(&q->usems, &usm->assoc, (u_long)sm->shadow_sem.sem);
	xnlock_put_irqrestore(&pse51_assoc_lock, s);

      got_usm:

	if (usm->uaddr == uaddr)
		/* First binding by this process. */
		err = __xn_safe_copy_to_user((void __user *)usm->uaddr,
					     &sm->shadow_sem, sizeof(sm->shadow_sem));
	else
		/* Semaphore already bound by this process in user-space. */
		err = __xn_safe_copy_to_user(u_addr, &usm->uaddr,
					     sizeof(unsigned long));

	return err;
}

static int __sem_close(unsigned long uaddr,
		       int __user *u_closed)
{
	pse51_assoc_t *assoc;
	union __xeno_sem sm;
	int closed = 0, err;
	pse51_queues_t *q;
	pse51_usem_t *usm;
	spl_t s;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	if (__xn_safe_copy_from_user(&sm.shadow_sem,
				     (void __user *)uaddr, sizeof(sm.shadow_sem)))
		return -EFAULT;

	xnlock_get_irqsave(&pse51_assoc_lock, s);

	assoc = pse51_assoc_lookup(&q->usems, (u_long)sm.shadow_sem.sem);
	if (assoc == NULL) {
		xnlock_put_irqrestore(&pse51_assoc_lock, s);
		return -EINVAL;
	}

	usm = assoc2usem(assoc);

	err = sem_close(&sm.native_sem);

	if (!err && (closed = (--usm->refcnt == 0)))
		pse51_assoc_remove(&q->usems, (u_long)sm.shadow_sem.sem);

	xnlock_put_irqrestore(&pse51_assoc_lock, s);

	if (err)
		return -thread_get_errno();

	if (closed)
		xnfree(usm);

	return __xn_safe_copy_to_user(u_closed, &closed, sizeof(int));
}

static int __sem_unlink(const char __user *u_name)
{
	char name[PSE51_MAXNAME];
	long len;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return len;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	return sem_unlink(name) == 0 ? 0 : -thread_get_errno();
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

#ifndef CONFIG_XENO_FASTSYNCH
static int __pthread_mutex_init(union __xeno_mutex __user *u_mx,
				const pthread_mutexattr_t __user *u_attr)
{
	pthread_mutexattr_t locattr, *attr;
	union __xeno_mutex mx;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	if (u_attr) {
		if (__xn_safe_copy_from_user(&locattr, u_attr,
					     sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = NULL;

	err = pthread_mutex_init(&mx.native_mutex, attr);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(&u_mx->shadow_mutex,
				      &mx.shadow_mutex, sizeof(u_mx->shadow_mutex));
}

static int __pthread_mutex_destroy(union __xeno_mutex __user *u_mx)
{
	union __xeno_mutex mx;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	err = pthread_mutex_destroy(&mx.native_mutex);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(&u_mx->shadow_mutex,
				      &mx.shadow_mutex, sizeof(u_mx->shadow_mutex));
}

static int __pthread_mutex_lock(union __xeno_mutex __user *u_mx)
{
	DECLARE_CB_LOCK_FLAGS(s);
	union __xeno_mutex mx;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	if (unlikely(cb_try_read_lock(&mx.shadow_mutex.lock, s)))
		return -EINVAL;

	err = pse51_mutex_timedlock_break(&mx.shadow_mutex, 0, XN_INFINITE);

	cb_read_unlock(&mx.shadow_mutex.lock, s);

	if (err == 0 &&
	    __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
				   &mx.shadow_mutex.lockcnt,
				   sizeof(u_mx->shadow_mutex.lockcnt)))
		return -EFAULT;

	return err;
}

static int __pthread_mutex_timedlock(union __xeno_mutex __user *u_mx,
				     const struct timespec __user *u_ts)
{
	DECLARE_CB_LOCK_FLAGS(s);
	union __xeno_mutex mx;
	struct timespec ts;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	if (unlikely(cb_try_read_lock(&mx.shadow_mutex.lock, s)))
		return -EINVAL;

	err = pse51_mutex_timedlock_break(&mx.shadow_mutex,
					  1, ts2ticks_ceil(&ts) + 1);

	cb_read_unlock(&mx.shadow_mutex.lock, s);

	if (err == 0 &&
	    __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
				   &mx.shadow_mutex.lockcnt,
				   sizeof(u_mx->shadow_mutex.lockcnt)))
		return -EFAULT;

	return err;
}

static int __pthread_mutex_trylock(union __xeno_mutex __user *u_mx)
{
	union __xeno_mutex mx;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	err = pthread_mutex_trylock(&mx.native_mutex);
	if (err == 0 &&
	    __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
				   &mx.shadow_mutex.lockcnt,
				   sizeof(u_mx->shadow_mutex.lockcnt)))
		return -EFAULT;

	return -err;
}

static int __pthread_mutex_unlock(union __xeno_mutex __user *u_mx)
{
	xnthread_t *cur = xnpod_current_thread();
	struct __shadow_mutex *shadow;
	DECLARE_CB_LOCK_FLAGS(s);
	union __xeno_mutex mx;
	pse51_mutex_t *mutex;
	int err;

	if (xnpod_root_p())
		return -EPERM;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	shadow = &mx.shadow_mutex;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return -EINVAL;

	if (!pse51_obj_active(shadow,
			      PSE51_MUTEX_MAGIC, struct __shadow_mutex)) {
		err = -EINVAL;
		goto out;
	}

	mutex = shadow->mutex;

	err = (xnsynch_owner(&mutex->synchbase) == cur) ? 0 : -EPERM;
	if (err)
		goto out;

	if (shadow->lockcnt > 1) {
		/* Mutex is recursive */
		--shadow->lockcnt;
		cb_read_unlock(&shadow->lock, s);

		if (__xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
					   &shadow->lockcnt,
					   sizeof(u_mx->shadow_mutex.lockcnt)))
			return -EFAULT;

		return 0;
	}

	if (xnsynch_release(&mutex->synchbase))
		xnpod_schedule();

  out:
	cb_read_unlock(&shadow->lock, s);

	return err;
}
#else /* !CONFIG_XENO_FASTSYNCH */
static int __pthread_mutex_check_init(union __xeno_mutex __user *u_mx,
				      const pthread_mutexattr_t __user *u_attr)
{
	pthread_mutexattr_t locattr, *attr;
	union __xeno_mutex mx;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	if (u_attr) {
		if (__xn_safe_copy_from_user(&locattr, u_attr,
					     sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = NULL;

	return pse51_mutex_check_init(&u_mx->shadow_mutex, attr);
}

static int __pthread_mutex_init(union __xeno_mutex __user *u_mx,
				const pthread_mutexattr_t __user *u_attr)
{
	pthread_mutexattr_t locattr, *attr;
	xnarch_atomic_t *ownerp;
	union __xeno_mutex mx;
	pse51_mutex_t *mutex;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	if (u_attr) {
		if (__xn_safe_copy_from_user(&locattr, u_attr,
					     sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = &pse51_default_mutex_attr;

	mutex = xnmalloc(sizeof(*mutex));
	if (mutex == NULL)
		return -ENOMEM;

	ownerp = xnheap_alloc(&xnsys_ppd_get(attr->pshared)->sem_heap,
			      sizeof(xnarch_atomic_t));
	if (ownerp == NULL) {
		xnfree(mutex);
		return -EAGAIN;
	}

	err = pse51_mutex_init_internal(&mx.shadow_mutex, mutex, ownerp, attr);
	if (err) {
		xnfree(mutex);
		xnheap_free(&xnsys_ppd_get(attr->pshared)->sem_heap, ownerp);
		return err;
	}

	return __xn_safe_copy_to_user(&u_mx->shadow_mutex,
				      &mx.shadow_mutex, sizeof(u_mx->shadow_mutex));
}

static int __pthread_mutex_destroy(union __xeno_mutex __user *u_mx)
{
	struct __shadow_mutex *shadow;
	union __xeno_mutex mx;
	pse51_mutex_t *mutex;

	shadow = &mx.shadow_mutex;

	if (__xn_safe_copy_from_user(shadow,
				     &u_mx->shadow_mutex,
				     sizeof(*shadow)))
		return -EFAULT;

	if (!pse51_obj_active(shadow, PSE51_MUTEX_MAGIC, struct __shadow_mutex))
		return -EINVAL;

	mutex = shadow->mutex;
	if (pse51_kqueues(mutex->attr.pshared) != mutex->owningq)
		return -EPERM;

	if (xnsynch_fast_owner_check(mutex->synchbase.fastlock,
				     XN_NO_HANDLE) != 0)
		return -EBUSY;

	pse51_mark_deleted(shadow);
	pse51_mutex_destroy_internal(mutex, mutex->owningq);

	return __xn_safe_copy_to_user(&u_mx->shadow_mutex,
				      shadow, sizeof(u_mx->shadow_mutex));
}

static int __pthread_mutex_lock(union __xeno_mutex __user *u_mx)
{
	struct __shadow_mutex *shadow;
	union __xeno_mutex mx;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     offsetof(struct __shadow_mutex, lock)))
		return -EFAULT;

	shadow = &mx.shadow_mutex;

	err = pse51_mutex_timedlock_break(&mx.shadow_mutex, 0, XN_INFINITE);
	if (err == 0 &&
	    __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
				   &shadow->lockcnt,
				   sizeof(u_mx->shadow_mutex.lockcnt)))
		return -EFAULT;

	return err;
}

static int __pthread_mutex_timedlock(union __xeno_mutex __user *u_mx,
				     const struct timespec __user *u_ts)
{
	struct __shadow_mutex *shadow;
	union __xeno_mutex mx;
	struct timespec ts;
	int err;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     sizeof(mx.shadow_mutex)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	shadow = &mx.shadow_mutex;

	err = pse51_mutex_timedlock_break(&mx.shadow_mutex,
					  1, ts2ticks_ceil(&ts) + 1);
	if (err == 0 &&
	    __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
				   &shadow->lockcnt,
				   sizeof(u_mx->shadow_mutex.lockcnt)))
		return -EFAULT;

	return err;
}

static int __pthread_mutex_unlock(union __xeno_mutex __user *u_mx)
{
	union __xeno_mutex mx;

	if (xnpod_root_p())
		return -EPERM;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
				     offsetof(struct __shadow_mutex, lock)))
		return -EFAULT;

	if (xnsynch_release(&mx.shadow_mutex.mutex->synchbase))
		xnpod_schedule();

	return 0;
}
#endif /* !CONFIG_XENO_FASTSYNCH */

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

static int __pthread_cond_init(union __xeno_cond __user *u_cnd,
			       const pthread_condattr_t __user *u_attr)
{
	pthread_condattr_t locattr, *attr;
	union __xeno_cond cnd;
	int err;

	if (__xn_safe_copy_from_user(&cnd.shadow_cond,
				     &u_cnd->shadow_cond,
				     sizeof(cnd.shadow_cond)))
		return -EFAULT;

	if (u_attr) {
		if (__xn_safe_copy_from_user(&locattr,
					     u_attr, sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = NULL;

	/* Always use default attribute. */
	err = pthread_cond_init(&cnd.native_cond, attr);

	if (err)
		return -err;

	return __xn_safe_copy_to_user(&u_cnd->shadow_cond,
				      &cnd.shadow_cond, sizeof(u_cnd->shadow_cond));
}

static int __pthread_cond_destroy(union __xeno_cond __user *u_cnd)
{
	union __xeno_cond cnd;
	int err;

	if (__xn_safe_copy_from_user(&cnd.shadow_cond,
				     &u_cnd->shadow_cond,
				     sizeof(cnd.shadow_cond)))
		return -EFAULT;

	err = pthread_cond_destroy(&cnd.native_cond);
	if (err)
		return -err;

	return __xn_safe_copy_to_user(&u_cnd->shadow_cond,
				      &cnd.shadow_cond, sizeof(u_cnd->shadow_cond));
}

struct us_cond_data {
	unsigned count;
	int err;
};

/* pthread_cond_wait_prologue(cond, mutex, count_ptr, timed, timeout) */
static int __pthread_cond_wait_prologue(union __xeno_cond __user *u_cnd,
					union __xeno_mutex __user *u_mx,
					struct us_cond_data __user *u_d,
					unsigned int timed,
					struct timespec __user *u_ts)
{
	xnthread_t *cur = xnshadow_thread(current);
	struct us_cond_data d;
	union __xeno_cond cnd;
	union __xeno_mutex mx;
	struct timespec ts;
	int err, perr = 0;

	if (__xn_safe_copy_from_user(&cnd.shadow_cond,
				     &u_cnd->shadow_cond,
				     sizeof(cnd.shadow_cond)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
#ifdef CONFIG_XENO_FASTSYNCH
				     offsetof(struct __shadow_mutex, lock)
#else /* !CONFIG_XENO_FASTSYNCH */
				     sizeof(mx.shadow_mutex)
#endif /* !CONFIG_XENO_FASTSYNCH */
				     ))
		return -EFAULT;

	if (timed) {
		if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
			return -EFAULT;

		err = pse51_cond_timedwait_prologue(cur,
						    &cnd.shadow_cond,
						    &mx.shadow_mutex,
						    &d.count,
						    timed,
						    ts2ticks_ceil(&ts) + 1);
	} else
		err = pse51_cond_timedwait_prologue(cur,
						    &cnd.shadow_cond,
						    &mx.shadow_mutex,
						    &d.count,
						    timed, XN_INFINITE);

	switch(err) {
	case 0:
	case ETIMEDOUT:
		perr = d.err = err;
		err = -pse51_cond_timedwait_epilogue(cur, &cnd.shadow_cond,
					    	    &mx.shadow_mutex, d.count);
		if (err == 0 &&
		    __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
					   &mx.shadow_mutex.lockcnt,
					   sizeof(u_mx->shadow_mutex.lockcnt)))
			return -EFAULT;
		break;

	case EINTR:
		perr = err;
		d.err = 0;	/* epilogue should return 0. */
		break;
	}

	if (err == EINTR 
	    &&__xn_safe_copy_to_user(u_d, &d, sizeof(d)))
			return -EFAULT;

	return err == 0 ? -perr : -err;
}

static int __pthread_cond_wait_epilogue(union __xeno_cond __user *u_cnd,
					union __xeno_mutex __user *u_mx,
					unsigned int count)
{
	xnthread_t *cur = xnshadow_thread(current);
	union __xeno_cond cnd;
	union __xeno_mutex mx;
	int err;

	if (__xn_safe_copy_from_user(&cnd.shadow_cond,
				     &u_cnd->shadow_cond,
				     sizeof(cnd.shadow_cond)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&mx.shadow_mutex,
				     &u_mx->shadow_mutex,
#ifdef CONFIG_XENO_FASTSYNCH
				     offsetof(struct __shadow_mutex, lock)
#else /* !CONFIG_XENO_FASTSYNCH */
				     sizeof(mx.shadow_mutex)
#endif /* !CONFIG_XENO_FASTSYNCH */
				     ))
		return -EFAULT;

	err = pse51_cond_timedwait_epilogue(cur, &cnd.shadow_cond,
					    &mx.shadow_mutex, count);

	if (err == 0
	    && __xn_safe_copy_to_user(&u_mx->shadow_mutex.lockcnt,
				      &mx.shadow_mutex.lockcnt,
				      sizeof(u_mx->shadow_mutex.lockcnt)))
		return -EFAULT;

	return err;
}

static int __pthread_cond_signal(union __xeno_cond __user *u_cnd)
{
	union __xeno_cond cnd;

	if (__xn_safe_copy_from_user(&cnd.shadow_cond,
				     &u_cnd->shadow_cond,
				     sizeof(cnd.shadow_cond)))
		return -EFAULT;

	return -pthread_cond_signal(&cnd.native_cond);
}

static int __pthread_cond_broadcast(union __xeno_cond __user *u_cnd)
{
	union __xeno_cond cnd;

	if (__xn_safe_copy_from_user(&cnd.shadow_cond,
				     &u_cnd->shadow_cond,
				     sizeof(cnd.shadow_cond)))
		return -EFAULT;

	return -pthread_cond_broadcast(&cnd.native_cond);
}

/* mq_open(name, oflags, mode, attr, ufd) */
static int __mq_open(const char __user *u_name,
		     int oflags,
		     mode_t mode,
		     struct mq_attr __user *u_attr,
		     mqd_t uqd)
{
	struct mq_attr locattr, *attr;
	char name[PSE51_MAXNAME];
	pse51_ufd_t *assoc;
	pse51_queues_t *q;
	unsigned len;
	mqd_t kqd;
	int err;

	q = pse51_queues();
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

	err = pse51_assoc_insert(&q->uqds, &assoc->assoc, (u_long)uqd);
	if (err) {
		xnfree(assoc);
		mq_close(kqd);
	}

	return err;
}

static int __mq_close(mqd_t uqd)
{
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_remove(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	err = mq_close(assoc2ufd(assoc)->kfd);
	xnfree(assoc2ufd(assoc));

	return !err ? 0 : -thread_get_errno();
}

static int __mq_unlink(const char __user *u_name)
{
	char name[PSE51_MAXNAME];
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
	pse51_assoc_t *assoc;
	struct mq_attr attr;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
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
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
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
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	pse51_msg_t *msg;
	pse51_ufd_t *ufd;
	pse51_mq_t *mq;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (len > 0 && !access_rok(u_buf, len))
		return -EFAULT;

	msg = pse51_mq_timedsend_inner(&mq, ufd->kfd, len, NULL);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if(__xn_copy_from_user(msg->data, u_buf, len)) {
		pse51_mq_finish_send(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	msg->len = len;
	pse51_msg_set_prio(msg, prio);

	return pse51_mq_finish_send(ufd->kfd, mq, msg);
}

static int __mq_timedsend(mqd_t uqd,
			  const void __user *u_buf,
			  size_t len,
			  unsigned int prio,
			  const struct timespec __user *u_ts)
{
	struct timespec timeout, *timeoutp;
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	pse51_msg_t *msg;
	pse51_ufd_t *ufd;
	pse51_mq_t *mq;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
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

	msg = pse51_mq_timedsend_inner(&mq, ufd->kfd, len, timeoutp);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if(__xn_copy_from_user(msg->data, u_buf, len)) {
		pse51_mq_finish_send(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	msg->len = len;
	pse51_msg_set_prio(msg, prio);

	return pse51_mq_finish_send(ufd->kfd, mq, msg);
}

static int __mq_receive(mqd_t uqd,
			void __user *u_buf,
			ssize_t __user *u_len,
			unsigned int __user *u_prio)
{
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;
	pse51_msg_t *msg;
	pse51_mq_t *mq;
	unsigned prio;
	ssize_t len;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&len, u_len, sizeof(len)))
		return -EFAULT;

	if (u_prio && !access_wok(u_prio, sizeof(prio)))
		return -EFAULT;

	if (len > 0 && !access_wok(u_buf, len))
		return -EFAULT;

	msg = pse51_mq_timedrcv_inner(&mq, ufd->kfd, len, NULL);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_to_user(u_buf, msg->data, msg->len)) {
		pse51_mq_finish_rcv(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	len = msg->len;
	prio = pse51_msg_get_prio(msg);

	err = pse51_mq_finish_rcv(ufd->kfd, mq, msg);
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
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	unsigned int prio;
	pse51_ufd_t *ufd;
	pse51_msg_t *msg;
	pse51_mq_t *mq;
	ssize_t len;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
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

	msg = pse51_mq_timedrcv_inner(&mq, ufd->kfd, len, timeoutp);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_to_user(u_buf, msg->data, msg->len)) {
		pse51_mq_finish_rcv(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	len = msg->len;
	prio = pse51_msg_get_prio(msg);

	err = pse51_mq_finish_rcv(ufd->kfd, mq, msg);
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
	pse51_assoc_t *assoc;
	struct sigevent sev;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, (u_long)uqd);
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
			if (__xn_safe_copy_from_user(&sm.shadow_sem,
						     &u_sem->shadow_sem,
						     sizeof(sm.shadow_sem)))
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

#ifdef CONFIG_XENO_OPT_POSIX_SELECT
static int fd_valid_p(int fd)
{
	pse51_queues_t *q;
	pse51_assoc_t *assoc;
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

	q = pse51_queues();
	if (q == NULL)
		return 0;

	return pse51_assoc_lookup(&q->uqds, fd) != NULL;
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
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	const int rtdm_fd_start = FD_SETSIZE - RTDM_FD_MAX;

	if (fd >= rtdm_fd_start)
		return rtdm_select_bind(fd - rtdm_fd_start,
					selector, type, fd);

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->uqds, fd);
	if (assoc == NULL)
		return -EBADF;

	return pse51_mq_select_bind(assoc2ufd(assoc)->kfd, selector, type, fd);
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

		timeout = clock_get_ticks(CLOCK_MONOTONIC) + tv2ticks_ceil(&tv);
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
#else /* !CONFIG_XENO_OPT_POSIX_SELECT */
#define __select __pse51_call_not_available
#endif /* !CONFIG_XENO_OPT_POSIX_SELECT */

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
	char name[PSE51_MAXNAME];
	pse51_ufd_t *assoc;
	pse51_queues_t *q;
	int kfd, err, len;

	q = pse51_queues();
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
		pse51_shm_close(kfd);
		return -ENOSPC;
	}

	assoc->kfd = kfd;

	err = pse51_assoc_insert(&q->ufds, &assoc->assoc, fd);
	if (err) {
		xnfree(assoc);
		close(kfd);
	}

	return err;
}

static int __shm_unlink(const char __user *u_name)
{
	char name[PSE51_MAXNAME];
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
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_remove(&q->ufds, fd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	err = close(ufd->kfd);
	xnfree(ufd);

	return err == 0 ? 0 : -thread_get_errno();
}

static int __ftruncate(int fd, off_t len)
{
	pse51_assoc_t *assoc;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->ufds, fd);
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
} pse51_mmap_param_t;

static int __mmap_prologue(size_t len,
			   int fd,
			   off_t off,
			   pse51_mmap_param_t __user *u_param)
{
	pse51_mmap_param_t mmap_param;
	pse51_assoc_t *assoc;
	struct xnheap *heap;
	pse51_queues_t *q;
	pse51_ufd_t *ufd;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->ufds, fd);
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
	     pse51_xnheap_get(&mmap_param.ioctl_cookie, mmap_param.kaddr))) {
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
			   pse51_mmap_param_t __user *u_param)
{
	pse51_mmap_param_t mmap_param;
	pse51_umap_t *umap;
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

	err = pse51_assoc_insert(&pse51_queues()->umaps,
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
	pse51_assoc_t *assoc;
	pse51_umap_t *umap;
	pse51_queues_t *q;
	xnheap_t *heap;
	int err;

	q = pse51_queues();
	if (q == NULL)
		return -EPERM;

	assoc = pse51_assoc_lookup(&q->umaps, (u_long)u_addr);
	if (assoc == NULL)
		return -EBADF;

	umap = assoc2umap(assoc);

	err = pse51_xnheap_get(&heap, umap->kaddr);
	if (err)
		return err;

	uunmap.mapsize = xnheap_extentsize(heap);
	uunmap.offset = xnheap_mapped_offset(heap, umap->kaddr);

	return __xn_safe_copy_to_user(u_unmap, &uunmap, sizeof(uunmap));
}

static int __munmap_epilogue(void __user *u_addr,
			     size_t len)
{
	pse51_assoc_t *assoc;
	pse51_umap_t *umap;
	spl_t s;
	int err;

	xnlock_get_irqsave(&pse51_assoc_lock, s);

	assoc = pse51_assoc_lookup(&pse51_queues()->umaps, (u_long)u_addr);
	if (assoc == NULL) {
		xnlock_put_irqrestore(&pse51_assoc_lock, s);
		return -EBADF;
	}

	umap = assoc2umap(assoc);

	if (umap->len != len) {
		xnlock_put_irqrestore(&pse51_assoc_lock, s);
		return -EINVAL;
	}

	pse51_assoc_remove(&pse51_queues()->umaps, (u_long)u_addr);
	xnlock_put_irqrestore(&pse51_assoc_lock, s);

	err = munmap(umap->kaddr, len);
	if (err == 0)
		xnfree(umap);

	return err == 0 ? 0 : -thread_get_errno();
}
#else /* !CONFIG_XENO_OPT_POSIX_SHM */

#define __shm_open        __pse51_call_not_available
#define __shm_unlink      __pse51_call_not_available
#define __shm_close       __pse51_call_not_available
#define __ftruncate       __pse51_call_not_available
#define __mmap_prologue   __pse51_call_not_available
#define __mmap_epilogue   __pse51_call_not_available
#define __munmap_prologue __pse51_call_not_available
#define __munmap_epilogue __pse51_call_not_available

#endif /* !CONFIG_XENO_OPT_POSIX_SHM */

int __pse51_call_not_available(void)
{
	return -ENOSYS;
}

static struct xnsysent __systab[] = {
	SKINCALL_DEF(__pse51_thread_create, __pthread_create, init),
	SKINCALL_DEF(__pse51_thread_detach, __pthread_detach, any),
	SKINCALL_DEF(__pse51_thread_setschedparam, __pthread_setschedparam, conforming),
	SKINCALL_DEF(__pse51_thread_setschedparam_ex, __pthread_setschedparam_ex, conforming),
	SKINCALL_DEF(__pse51_thread_getschedparam, __pthread_getschedparam, any),
	SKINCALL_DEF(__pse51_thread_getschedparam_ex, __pthread_getschedparam_ex, any),
	SKINCALL_DEF(__pse51_sched_yield, __sched_yield, primary),
	SKINCALL_DEF(__pse51_thread_make_periodic, __pthread_make_periodic_np, conforming),
	SKINCALL_DEF(__pse51_thread_wait, __pthread_wait_np, primary),
	SKINCALL_DEF(__pse51_thread_set_mode, __pthread_set_mode_np, primary),
	SKINCALL_DEF(__pse51_thread_set_name, __pthread_set_name_np, any),
	SKINCALL_DEF(__pse51_thread_probe, __pthread_probe_np, any),
	SKINCALL_DEF(__pse51_thread_kill, __pthread_kill, any),
	SKINCALL_DEF(__pse51_sem_init, __sem_init, any),
	SKINCALL_DEF(__pse51_sem_destroy, __sem_destroy, any),
	SKINCALL_DEF(__pse51_sem_post, __sem_post, any),
	SKINCALL_DEF(__pse51_sem_wait, __sem_wait, primary),
	SKINCALL_DEF(__pse51_sem_timedwait, __sem_timedwait, primary),
	SKINCALL_DEF(__pse51_sem_trywait, __sem_trywait, primary),
	SKINCALL_DEF(__pse51_sem_getvalue, __sem_getvalue, any),
	SKINCALL_DEF(__pse51_sem_open, __sem_open, any),
	SKINCALL_DEF(__pse51_sem_close, __sem_close, any),
	SKINCALL_DEF(__pse51_sem_unlink, __sem_unlink, any),
	SKINCALL_DEF(__pse51_clock_getres, __clock_getres, any),
	SKINCALL_DEF(__pse51_clock_gettime, __clock_gettime, any),
	SKINCALL_DEF(__pse51_clock_settime, __clock_settime, any),
	SKINCALL_DEF(__pse51_clock_nanosleep, __clock_nanosleep, nonrestartable),
	SKINCALL_DEF(__pse51_mutex_init, __pthread_mutex_init, any),
	SKINCALL_DEF(__pse51_mutex_destroy, __pthread_mutex_destroy, any),
	SKINCALL_DEF(__pse51_mutex_lock, __pthread_mutex_lock, primary),
	SKINCALL_DEF(__pse51_mutex_timedlock, __pthread_mutex_timedlock, primary),
#ifndef CONFIG_XENO_FASTSYNCH
	SKINCALL_DEF(__pse51_mutex_trylock, __pthread_mutex_trylock, primary),
#else
        SKINCALL_DEF(__pse51_check_init, __pthread_mutex_check_init, any),
#endif
	SKINCALL_DEF(__pse51_mutex_unlock, __pthread_mutex_unlock, nonrestartable),
	SKINCALL_DEF(__pse51_cond_init, __pthread_cond_init, any),
	SKINCALL_DEF(__pse51_cond_destroy, __pthread_cond_destroy, any),
	SKINCALL_DEF(__pse51_cond_wait_prologue, __pthread_cond_wait_prologue, nonrestartable),
	SKINCALL_DEF(__pse51_cond_wait_epilogue, __pthread_cond_wait_epilogue, primary),
	SKINCALL_DEF(__pse51_cond_signal, __pthread_cond_signal, any),
	SKINCALL_DEF(__pse51_cond_broadcast, __pthread_cond_broadcast, any),
	SKINCALL_DEF(__pse51_mq_open, __mq_open, lostage),
	SKINCALL_DEF(__pse51_mq_close, __mq_close, lostage),
	SKINCALL_DEF(__pse51_mq_unlink, __mq_unlink, lostage),
	SKINCALL_DEF(__pse51_mq_getattr, __mq_getattr, any),
	SKINCALL_DEF(__pse51_mq_setattr, __mq_setattr, any),
	SKINCALL_DEF(__pse51_mq_send, __mq_send, primary),
	SKINCALL_DEF(__pse51_mq_timedsend, __mq_timedsend, primary),
	SKINCALL_DEF(__pse51_mq_receive, __mq_receive, primary),
	SKINCALL_DEF(__pse51_mq_timedreceive, __mq_timedreceive, primary),
	SKINCALL_DEF(__pse51_mq_notify, __mq_notify, primary),
	SKINCALL_DEF(__pse51_timer_create, __timer_create, any),
	SKINCALL_DEF(__pse51_timer_delete, __timer_delete, any),
	SKINCALL_DEF(__pse51_timer_settime, __timer_settime, primary),
	SKINCALL_DEF(__pse51_timer_gettime, __timer_gettime, any),
	SKINCALL_DEF(__pse51_timer_getoverrun, __timer_getoverrun, any),
	SKINCALL_DEF(__pse51_shm_open, __shm_open, lostage),
	SKINCALL_DEF(__pse51_shm_unlink, __shm_unlink, lostage),
	SKINCALL_DEF(__pse51_shm_close, __shm_close, lostage),
	SKINCALL_DEF(__pse51_ftruncate, __ftruncate, lostage),
	SKINCALL_DEF(__pse51_mmap_prologue, __mmap_prologue, lostage),
	SKINCALL_DEF(__pse51_mmap_epilogue, __mmap_epilogue, lostage),
	SKINCALL_DEF(__pse51_munmap_prologue, __munmap_prologue, lostage),
	SKINCALL_DEF(__pse51_munmap_epilogue, __munmap_epilogue, lostage),
	SKINCALL_DEF(__pse51_mutexattr_init, __pthread_mutexattr_init, any),
	SKINCALL_DEF(__pse51_mutexattr_destroy, __pthread_mutexattr_destroy, any),
	SKINCALL_DEF(__pse51_mutexattr_gettype, __pthread_mutexattr_gettype, any),
	SKINCALL_DEF(__pse51_mutexattr_settype, __pthread_mutexattr_settype, any),
	SKINCALL_DEF(__pse51_mutexattr_getprotocol, __pthread_mutexattr_getprotocol, any),
	SKINCALL_DEF(__pse51_mutexattr_setprotocol, __pthread_mutexattr_setprotocol, any),
	SKINCALL_DEF(__pse51_mutexattr_getpshared, __pthread_mutexattr_getpshared, any),
	SKINCALL_DEF(__pse51_mutexattr_setpshared, __pthread_mutexattr_setpshared, any),
	SKINCALL_DEF(__pse51_condattr_init, __pthread_condattr_init, any),
	SKINCALL_DEF(__pse51_condattr_destroy, __pthread_condattr_destroy, any),
	SKINCALL_DEF(__pse51_condattr_getclock, __pthread_condattr_getclock, any),
	SKINCALL_DEF(__pse51_condattr_setclock, __pthread_condattr_setclock, any),
	SKINCALL_DEF(__pse51_condattr_getpshared, __pthread_condattr_getpshared, any),
	SKINCALL_DEF(__pse51_condattr_setpshared, __pthread_condattr_setpshared, any),
	SKINCALL_DEF(__pse51_select, __select, primary),
	SKINCALL_DEF(__pse51_sched_minprio, __sched_min_prio, any),
	SKINCALL_DEF(__pse51_sched_maxprio, __sched_max_prio, any),
};

static void __shadow_delete_hook(xnthread_t *thread)
{
	if (xnthread_get_magic(thread) == PSE51_SKIN_MAGIC &&
	    xnthread_test_state(thread, XNSHADOW)) {
		pthread_t k_tid = thread2pthread(thread);
		__pthread_unhash(&k_tid->hkey);
		if (xnthread_test_state(thread, XNMAPPED))
			xnshadow_unmap(thread);
	}
}

static void *pse51_eventcb(int event, void *data)
{
	pse51_queues_t *q;

	switch (event) {
	case XNSHADOW_CLIENT_ATTACH:
		q = (pse51_queues_t *) xnarch_alloc_host_mem(sizeof(*q));
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
		pse51_assocq_init(&q->uqds);
		pse51_assocq_init(&q->usems);
#ifdef CONFIG_XENO_OPT_POSIX_SHM
		pse51_assocq_init(&q->umaps);
		pse51_assocq_init(&q->ufds);
#endif /* CONFIG_XENO_OPT_POSIX_SHM */

		return &q->ppd;

	case XNSHADOW_CLIENT_DETACH:
		q = ppd2queues((xnshadow_ppd_t *) data);

#ifdef CONFIG_XENO_OPT_POSIX_SHM
		pse51_shm_ufds_cleanup(q);
		pse51_shm_umaps_cleanup(q);
#endif /* CONFIG_XENO_OPT_POSIX_SHM */
		pse51_sem_usems_cleanup(q);
		pse51_mq_uqds_cleanup(q);
		pse51_timerq_cleanup(&q->kqueues);
		pse51_semq_cleanup(&q->kqueues);
		pse51_mutexq_cleanup(&q->kqueues);
#ifdef CONFIG_XENO_OPT_POSIX_INTR
		pse51_intrq_cleanup(&q->kqueues);
#endif /* CONFIG_XENO_OPT_POSIX_INTR */
		pse51_condq_cleanup(&q->kqueues);

		xnarch_free_host_mem(q, sizeof(*q));

		return NULL;
	}

	return ERR_PTR(-EINVAL);
}

extern xntbase_t *pse51_tbase;

static struct xnskin_props __props = {
	.name = "posix",
	.magic = PSE51_SKIN_MAGIC,
	.nrcalls = sizeof(__systab) / sizeof(__systab[0]),
	.systab = __systab,
	.eventcb = &pse51_eventcb,
	.timebasep = &pse51_tbase,
	.module = THIS_MODULE
};

int pse51_syscall_init(void)
{
	pse51_muxid = xnshadow_register_interface(&__props);

	if (pse51_muxid < 0)
		return -ENOSYS;

	xnpod_add_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);

	return 0;
}

void pse51_syscall_cleanup(void)
{
	xnpod_remove_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);
	xnshadow_unregister_interface(pse51_muxid);
}
