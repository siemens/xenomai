/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _COBALT_POSIX_SYSCALL32_H
#define _COBALT_POSIX_SYSCALL32_H

#include <linux/compat.h>
#include <net/compat.h>
#include "thread.h"
#include "mutex.h"
#include "cond.h"
#include "sem.h"
#include "sched.h"
#include "mqueue.h"
#include "clock.h"
#include "timer.h"
#include "timerfd.h"
#include "signal.h"
#include "monitor.h"
#include "event.h"
#include "io.h"

struct __compat_sched_ss_param {
	int __sched_low_priority;
	struct compat_timespec __sched_repl_period;
	struct compat_timespec __sched_init_budget;
	int __sched_max_repl;
};

struct __compat_sched_rr_param {
	struct compat_timespec __sched_rr_quantum;
};

struct compat_sched_param_ex {
	int sched_priority;
	union {
		struct __compat_sched_ss_param ss;
		struct __compat_sched_rr_param rr;
		struct __sched_tp_param tp;
		struct __sched_quota_param quota;
	} sched_u;
};

struct compat_mq_attr {
	compat_long_t mq_flags;
	compat_long_t mq_maxmsg;
	compat_long_t mq_msgsize;
	compat_long_t mq_curmsgs;
};

struct compat_sched_tp_window {
	struct compat_timespec offset;
	struct compat_timespec duration;
	int ptid;
};

struct __compat_sched_config_tp {
	int nr_windows;
	struct compat_sched_tp_window windows[0];
};

union compat_sched_config {
	struct __compat_sched_config_tp tp;
	struct __sched_config_quota quota;
};

#define compat_sched_tp_confsz(nr_win) \
  (sizeof(struct __compat_sched_config_tp) + nr_win * sizeof(struct compat_sched_tp_window))

typedef struct {
	compat_ulong_t fds_bits[__FD_SETSIZE / (8 * sizeof(compat_long_t))];
} compat_fd_set;

struct compat_rtdm_mmap_request {
	compat_size_t length;
	compat_off_t offset;
	int prot;
	int flags;
};

COBALT_SYSCALL32emu_DECL(thread_create,
			 int, (compat_ulong_t pth,
			       int policy,
			       const struct compat_sched_param_ex __user *u_param_ex,
			       int xid,
			       __u32 __user *u_winoff));

COBALT_SYSCALL32emu_DECL(thread_setschedparam_ex,
			 int, (compat_ulong_t pth,
			       int policy,
			       const struct compat_sched_param_ex __user *u_param,
			       __u32 __user *u_winoff,
			       int __user *u_promoted));

COBALT_SYSCALL32emu_DECL(thread_getschedparam_ex,
			 int, (compat_ulong_t pth,
			       int __user *u_policy,
			       struct compat_sched_param_ex __user *u_param));

COBALT_SYSCALL32emu_DECL(clock_getres,
			 int, (clockid_t clock_id,
			       struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(clock_gettime,
			 int, (clockid_t clock_id,
			       struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(clock_settime,
			 int, (clockid_t clock_id,
			       const struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(clock_nanosleep,
			 int, (clockid_t clock_id, int flags,
			       const struct compat_timespec __user *u_rqt,
			       struct compat_timespec __user *u_rmt));

COBALT_SYSCALL32emu_DECL(mutex_timedlock,
			 int, (struct cobalt_mutex_shadow __user *u_mx,
			       const struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(cond_wait_prologue,
			 int, (struct cobalt_cond_shadow __user *u_cnd,
			       struct cobalt_mutex_shadow __user *u_mx,
			       int *u_err,
			       unsigned int timed,
			       struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_open,
			 int, (const char __user *u_name, int oflags,
			       mode_t mode, struct compat_mq_attr __user *u_attr));

COBALT_SYSCALL32emu_DECL(mq_getattr,
			 int, (mqd_t uqd, struct compat_mq_attr __user *u_attr));

COBALT_SYSCALL32emu_DECL(mq_setattr,
			 int, (mqd_t uqd, const struct compat_mq_attr __user *u_attr,
			       struct compat_mq_attr __user *u_oattr));

COBALT_SYSCALL32emu_DECL(mq_timedsend,
			 int, (mqd_t uqd, const void __user *u_buf, size_t len,
			       unsigned int prio,
			       const struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_timedreceive,
			 int, (mqd_t uqd, void __user *u_buf,
			       compat_ssize_t __user *u_len,
			       unsigned int __user *u_prio,
			       const struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_notify,
			 int, (mqd_t fd, const struct compat_sigevent *__user u_cev));

COBALT_SYSCALL32emu_DECL(sched_weightprio,
			 int, (int policy,
			       const struct compat_sched_param_ex __user *u_param));

COBALT_SYSCALL32emu_DECL(sched_setconfig_np,
			 int, (int cpu, int policy,
			       union compat_sched_config __user *u_config,
			       size_t len));

COBALT_SYSCALL32emu_DECL(sched_getconfig_np,
			 ssize_t, (int cpu, int policy,
				   union compat_sched_config __user *u_config,
				   size_t len));

COBALT_SYSCALL32emu_DECL(timer_create,
			 int, (clockid_t clock,
			       const struct compat_sigevent __user *u_sev,
			       timer_t __user *u_tm));

COBALT_SYSCALL32emu_DECL(timer_settime,
			 int, (timer_t tm, int flags,
			       const struct compat_itimerspec __user *u_newval,
			       struct compat_itimerspec __user *u_oldval));

COBALT_SYSCALL32emu_DECL(timer_gettime,
			 int, (timer_t tm,
			       struct compat_itimerspec __user *u_val));

COBALT_SYSCALL32emu_DECL(timerfd_settime,
			 int, (int fd, int flags,
			       const struct compat_itimerspec __user *new_value,
			       struct compat_itimerspec __user *old_value));

COBALT_SYSCALL32emu_DECL(timerfd_gettime,
			 int, (int fd, struct compat_itimerspec __user *value));

COBALT_SYSCALL32emu_DECL(sigwait,
			 int, (const compat_sigset_t __user *u_set,
			       int __user *u_sig));

COBALT_SYSCALL32emu_DECL(sigtimedwait,
			 int, (const compat_sigset_t __user *u_set,
			       struct compat_siginfo __user *u_si,
			       const struct compat_timespec __user *u_timeout));

COBALT_SYSCALL32emu_DECL(sigwaitinfo,
			 int, (const compat_sigset_t __user *u_set,
			       struct compat_siginfo __user *u_si));

COBALT_SYSCALL32emu_DECL(sigpending,
			 int, (compat_sigset_t __user *u_set));

COBALT_SYSCALL32emu_DECL(sigqueue,
			 int, (pid_t pid, int sig,
			       const union compat_sigval __user *u_value));

COBALT_SYSCALL32emu_DECL(monitor_wait,
			 int, (struct cobalt_monitor_shadow __user *u_mon,
			       int event, const struct compat_timespec __user *u_ts,
			       int __user *u_ret));

COBALT_SYSCALL32emu_DECL(event_wait,
			 int, (struct cobalt_event_shadow __user *u_event,
			       unsigned int bits,
			       unsigned int __user *u_bits_r,
			       int mode, const struct compat_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(select,
			 int, (int nfds,
			       compat_fd_set __user *u_rfds,
			       compat_fd_set __user *u_wfds,
			       compat_fd_set __user *u_xfds,
			       struct compat_timeval __user *u_tv));

COBALT_SYSCALL32emu_DECL(recvmsg,
			 ssize_t, (int fd, struct compat_msghdr __user *umsg,
				   int flags));

COBALT_SYSCALL32emu_DECL(sendmsg,
			 ssize_t, (int fd, struct compat_msghdr __user *umsg,
				   int flags));

COBALT_SYSCALL32emu_DECL(mmap,
			 int, (int fd,
			       struct compat_rtdm_mmap_request __user *u_rma,
			       compat_uptr_t __user *u_addrp));

COBALT_SYSCALL32emu_DECL(backtrace,
			 int, (int nr, compat_ulong_t __user *u_backtrace,
			       int reason));

COBALT_SYSCALL32emu_DECL(sem_open,
			 int, (compat_uptr_t __user *u_addrp,
			       const char __user *u_name,
			       int oflags, mode_t mode, unsigned int value));

COBALT_SYSCALL32emu_DECL(sem_timedwait,
			 int, (struct cobalt_sem_shadow __user *u_sem,
			       struct compat_timespec __user *u_ts));

#endif /* !_COBALT_POSIX_SYSCALL32_H */
