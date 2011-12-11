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
#include "timer.h"
#include "monitor.h"
#include "sched.h"
#include "clock.h"
#include <rtdm/rtdm_driver.h>
#define RTDM_FD_MAX CONFIG_XENO_OPT_RTDM_FILDES

int cobalt_muxid;

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

int __cobalt_call_not_available(void)
{
	return -ENOSYS;
}

static struct xnsysent __systab[] = {
	SKINCALL_DEF(sc_cobalt_thread_create, cobalt_thread_create, init),
	SKINCALL_DEF(sc_cobalt_thread_setschedparam, cobalt_thread_setschedparam, conforming),
	SKINCALL_DEF(sc_cobalt_thread_setschedparam_ex, cobalt_thread_setschedparam_ex, conforming),
	SKINCALL_DEF(sc_cobalt_thread_getschedparam, cobalt_thread_getschedparam, any),
	SKINCALL_DEF(sc_cobalt_thread_getschedparam_ex, cobalt_thread_getschedparam_ex, any),
	SKINCALL_DEF(sc_cobalt_sched_yield, cobalt_sched_yield, primary),
	SKINCALL_DEF(sc_cobalt_thread_make_periodic, cobalt_thread_make_periodic_np, conforming),
	SKINCALL_DEF(sc_cobalt_thread_wait, cobalt_thread_wait_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_mode, cobalt_thread_set_mode_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_name, cobalt_thread_set_name_np, any),
	SKINCALL_DEF(sc_cobalt_thread_probe, cobalt_thread_probe_np, any),
	SKINCALL_DEF(sc_cobalt_thread_kill, cobalt_thread_kill, any),
	SKINCALL_DEF(sc_cobalt_thread_getstat, cobalt_thread_stat, any),
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
	SKINCALL_DEF(sc_cobalt_clock_getres, cobalt_clock_getres, any),
	SKINCALL_DEF(sc_cobalt_clock_gettime, cobalt_clock_gettime, any),
	SKINCALL_DEF(sc_cobalt_clock_settime, cobalt_clock_settime, any),
	SKINCALL_DEF(sc_cobalt_clock_nanosleep, cobalt_clock_nanosleep, nonrestartable),
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
<<<<<<< HEAD
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
=======

	SKINCALL_DEF(sc_cobalt_mq_open, cobalt_mq_open, lostage),
	SKINCALL_DEF(sc_cobalt_mq_close, cobalt_mq_close, lostage),
	SKINCALL_DEF(sc_cobalt_mq_unlink, cobalt_mq_unlink, lostage),
	SKINCALL_DEF(sc_cobalt_mq_getattr, cobalt_mq_getattr, any),
	SKINCALL_DEF(sc_cobalt_mq_setattr, cobalt_mq_setattr, any),
	SKINCALL_DEF(sc_cobalt_mq_send, cobalt_mq_send, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedsend, cobalt_mq_timedsend, primary),
	SKINCALL_DEF(sc_cobalt_mq_receive, cobalt_mq_receive, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedreceive, cobalt_mq_timedreceive, primary),
>>>>>>> cobalt: move message queues syscalls to mq.c
	SKINCALL_DEF(sc_cobalt_timer_create, __timer_create, any),
	SKINCALL_DEF(sc_cobalt_timer_delete, __timer_delete, any),
	SKINCALL_DEF(sc_cobalt_timer_settime, __timer_settime, primary),
	SKINCALL_DEF(sc_cobalt_timer_gettime, __timer_gettime, any),
	SKINCALL_DEF(sc_cobalt_timer_getoverrun, __timer_getoverrun, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_init, cobalt_mutexattr_init, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_destroy, cobalt_mutexattr_destroy, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_gettype, cobalt_mutexattr_gettype, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_settype, cobalt_mutexattr_settype, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_getprotocol, cobalt_mutexattr_getprotocol, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_setprotocol, cobalt_mutexattr_setprotocol, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_getpshared, cobalt_mutexattr_getpshared, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_setpshared, cobalt_mutexattr_setpshared, any),
	SKINCALL_DEF(sc_cobalt_condattr_init, cobalt_condattr_init, any),
	SKINCALL_DEF(sc_cobalt_condattr_destroy, cobalt_condattr_destroy, any),
	SKINCALL_DEF(sc_cobalt_condattr_getclock, cobalt_condattr_getclock, any),
	SKINCALL_DEF(sc_cobalt_condattr_setclock, cobalt_condattr_setclock, any),
	SKINCALL_DEF(sc_cobalt_condattr_getpshared, cobalt_condattr_getpshared, any),
	SKINCALL_DEF(sc_cobalt_condattr_setpshared, cobalt_condattr_setpshared, any),
	SKINCALL_DEF(sc_cobalt_select, __select, primary),
	SKINCALL_DEF(sc_cobalt_sched_minprio, cobalt_sched_min_prio, any),
	SKINCALL_DEF(sc_cobalt_sched_maxprio, cobalt_sched_max_prio, any),
	SKINCALL_DEF(sc_cobalt_monitor_init, cobalt_monitor_init, any),
	SKINCALL_DEF(sc_cobalt_monitor_destroy, cobalt_monitor_destroy, any),
	SKINCALL_DEF(sc_cobalt_monitor_enter, cobalt_monitor_enter, primary),
	SKINCALL_DEF(sc_cobalt_monitor_wait, cobalt_monitor_wait, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_sync, cobalt_monitor_sync, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_exit, cobalt_monitor_exit, primary),
};

static void *cobalt_eventcb(int event, void *data)
{
	cobalt_queues_t *q;

	switch (event) {
	case XNSHADOW_CLIENT_ATTACH:
		q = (cobalt_queues_t *) xnarch_alloc_host_mem(sizeof(*q));
		if (q == NULL)
			return ERR_PTR(-ENOSPC);

		initq(&q->kqueues.condq);
		initq(&q->kqueues.mutexq);
		initq(&q->kqueues.semq);
		initq(&q->kqueues.threadq);
		initq(&q->kqueues.timerq);
		initq(&q->kqueues.monitorq);
		cobalt_assocq_init(&q->uqds);
		cobalt_assocq_init(&q->usems);

		return &q->ppd;

	case XNSHADOW_CLIENT_DETACH:
		q = ppd2queues((xnshadow_ppd_t *) data);

		cobalt_sem_usems_cleanup(q);
		cobalt_mq_uqds_cleanup(q);
		cobalt_monitorq_cleanup(&q->kqueues);
		cobalt_timerq_cleanup(&q->kqueues);
		cobalt_semq_cleanup(&q->kqueues);
		cobalt_mutexq_cleanup(&q->kqueues);
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

	return 0;
}

void cobalt_syscall_cleanup(void)
{
	xnshadow_unregister_interface(cobalt_muxid);
}
