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
#include <linux/types.h>
#include <linux/err.h>
#include <cobalt/uapi/syscall.h>
#include <xenomai/rtdm/internal.h>
#include "internal.h"
#include "syscall32.h"
#include "../debug.h"

static int sys32_get_timespec(struct timespec *ts,
			      const struct compat_timespec __user *cts)
{
	return (cts == NULL ||
		!access_rok(cts, sizeof(*cts)) ||
		__xn_get_user(ts->tv_sec, &cts->tv_sec) ||
		__xn_get_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static int sys32_put_timespec(struct compat_timespec __user *cts,
			      const struct timespec *ts)
{
	return (cts == NULL ||
		!access_wok(cts, sizeof(*cts)) ||
		__xn_put_user(ts->tv_sec, &cts->tv_sec) ||
		__xn_put_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static int sys32_get_itimerspec(struct itimerspec *its,
				const struct compat_itimerspec __user *cits)
{
	int ret = sys32_get_timespec(&its->it_value, &cits->it_value);

	return ret ?: sys32_get_timespec(&its->it_interval, &cits->it_interval);
}

static int sys32_put_itimerspec(struct compat_itimerspec __user *cits,
				const struct itimerspec *its)
{
	int ret = sys32_put_timespec(&cits->it_value, &its->it_value);

	return ret ?: sys32_put_timespec(&cits->it_interval, &its->it_interval);
}

static int sys32_get_timeval(struct timeval *tv,
			     const struct compat_timeval __user *ctv)
{
	return (ctv == NULL ||
		!access_rok(ctv, sizeof(*ctv)) ||
		__xn_get_user(tv->tv_sec, &ctv->tv_sec) ||
		__xn_get_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}

static int sys32_put_timeval(struct compat_timeval __user *ctv,
			     const struct timeval *tv)
{
	return (ctv == NULL ||
		!access_wok(ctv, sizeof(*ctv)) ||
		__xn_put_user(tv->tv_sec, &ctv->tv_sec) ||
		__xn_put_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}

static ssize_t sys32_get_fdset(fd_set *fds, const compat_fd_set __user *cfds,
			       size_t cfdsize)
{
	int rdpos, wrpos, rdlim = cfdsize / sizeof(compat_ulong_t);

	if (cfds == NULL || !access_rok(cfds, cfdsize))
		return -EFAULT;

	for (rdpos = 0, wrpos = 0; rdpos < rdlim; rdpos++, wrpos++)
		if (__xn_get_user(fds->fds_bits[wrpos], cfds->fds_bits + rdpos))
			return -EFAULT;

	return (ssize_t)rdlim * sizeof(long);
}

static ssize_t sys32_put_fdset(compat_fd_set __user *cfds, const fd_set *fds,
			       size_t fdsize)
{
	int rdpos, wrpos, wrlim = fdsize / sizeof(long);

	if (cfds == NULL || !access_wok(cfds, wrlim * sizeof(compat_ulong_t)))
		return -EFAULT;

	for (rdpos = 0, wrpos = 0; wrpos < wrlim; rdpos++, wrpos++)
		if (__xn_put_user(fds->fds_bits[rdpos], cfds->fds_bits + wrpos))
			return -EFAULT;

	return (ssize_t)wrlim * sizeof(compat_ulong_t);
}

static int sys32_get_param_ex(int policy,
			      struct sched_param_ex *p,
			      const struct compat_sched_param_ex __user *u_cp)
{
	struct compat_sched_param_ex cpex;

	if (u_cp == NULL || __xn_safe_copy_from_user(&cpex, u_cp, sizeof(cpex)))
		return -EFAULT;

	p->sched_priority = cpex.sched_priority;

	switch (policy) {
	case SCHED_SPORADIC:
		p->sched_ss_low_priority = cpex.sched_ss_low_priority;
		p->sched_ss_max_repl = cpex.sched_ss_max_repl;
		p->sched_ss_repl_period.tv_sec = cpex.sched_ss_repl_period.tv_sec;
		p->sched_ss_repl_period.tv_nsec = cpex.sched_ss_repl_period.tv_nsec;
		p->sched_ss_init_budget.tv_sec = cpex.sched_ss_init_budget.tv_sec;
		p->sched_ss_init_budget.tv_nsec = cpex.sched_ss_init_budget.tv_nsec;
		break;
	case SCHED_RR:
		p->sched_rr_quantum.tv_sec = cpex.sched_rr_quantum.tv_sec;
		p->sched_rr_quantum.tv_nsec = cpex.sched_rr_quantum.tv_nsec;
		break;
	case SCHED_TP:
		p->sched_tp_partition = cpex.sched_tp_partition;
		break;
	case SCHED_QUOTA:
		p->sched_quota_group = cpex.sched_quota_group;
		break;
	}

	return 0;
}

static int sys32_put_param_ex(int policy,
			      struct compat_sched_param_ex __user *u_cp,
			      const struct sched_param_ex *p)
{
	struct compat_sched_param_ex cpex;

	if (u_cp == NULL)
		return -EFAULT;

	cpex.sched_priority = p->sched_priority;

	switch (policy) {
	case SCHED_SPORADIC:
		cpex.sched_ss_low_priority = p->sched_ss_low_priority;
		cpex.sched_ss_max_repl = p->sched_ss_max_repl;
		cpex.sched_ss_repl_period.tv_sec = p->sched_ss_repl_period.tv_sec;
		cpex.sched_ss_repl_period.tv_nsec = p->sched_ss_repl_period.tv_nsec;
		cpex.sched_ss_init_budget.tv_sec = p->sched_ss_init_budget.tv_sec;
		cpex.sched_ss_init_budget.tv_nsec = p->sched_ss_init_budget.tv_nsec;
		break;
	case SCHED_RR:
		cpex.sched_rr_quantum.tv_sec = p->sched_rr_quantum.tv_sec;
		cpex.sched_rr_quantum.tv_nsec = p->sched_rr_quantum.tv_nsec;
		break;
	case SCHED_TP:
		cpex.sched_tp_partition = p->sched_tp_partition;
		break;
	case SCHED_QUOTA:
		cpex.sched_quota_group = p->sched_quota_group;
		break;
	}

	return __xn_safe_copy_to_user(u_cp, &cpex, sizeof(cpex));
}

static int sys32_get_mqattr(struct mq_attr *ap,
			    const struct compat_mq_attr __user *u_cap)
{
	struct compat_mq_attr cattr;

	if (u_cap == NULL ||
	    __xn_safe_copy_from_user(&cattr, u_cap, sizeof(cattr)))
		return -EFAULT;

	ap->mq_flags = cattr.mq_flags;
	ap->mq_maxmsg = cattr.mq_maxmsg;
	ap->mq_msgsize = cattr.mq_msgsize;
	ap->mq_curmsgs = cattr.mq_curmsgs;

	return 0;
}

static int sys32_put_mqattr(struct compat_mq_attr __user *u_cap,
			    const struct mq_attr *ap)
{
	struct compat_mq_attr cattr;

	cattr.mq_flags = ap->mq_flags;
	cattr.mq_maxmsg = ap->mq_maxmsg;
	cattr.mq_msgsize = ap->mq_msgsize;
	cattr.mq_curmsgs = ap->mq_curmsgs;

	return u_cap == NULL ? -EFAULT :
		__xn_safe_copy_to_user(u_cap, &cattr, sizeof(cattr));
}

static int sys32_get_sigevent(struct sigevent *ev,
			      const struct compat_sigevent *__user u_cev)
{
	struct compat_sigevent cev;
	compat_int_t *cp;
	int ret, *p;

	if (u_cev == NULL)
		return -EFAULT;

	ret = __xn_safe_copy_from_user(&cev, u_cev, sizeof(cev));
	if (ret)
		return ret;

	memset(ev, 0, sizeof(*ev));
	ev->sigev_value.sival_int = cev.sigev_value.sival_int;
	ev->sigev_signo = cev.sigev_signo;
	ev->sigev_notify = cev.sigev_notify;
	/*
	 * Extensions may define extra fields we don't know about in
	 * the padding area, so we have to load it entirely.
	 */
	p = ev->_sigev_un._pad;
	cp = cev._sigev_un._pad;
	while ((void *)cp < (void *)cev._sigev_un._pad
	       + sizeof(cev._sigev_un._pad))
		*p++ = *cp++;

	return 0;
}

static int sys32_get_sigset(sigset_t *set, const compat_sigset_t *u_cset)
{
	compat_sigset_t cset;
	int ret;

	if (u_cset == NULL)
		return -EFAULT;

	ret = __xn_safe_copy_from_user(&cset, u_cset, sizeof(cset));
	if (ret)
		return ret;

	sigset_from_compat(set, &cset);

	return 0;
}

static int sys32_put_sigset(compat_sigset_t *u_cset, const sigset_t *set)
{
	compat_sigset_t cset;

	if (u_cset == NULL)
		return -EFAULT;

	sigset_to_compat(&cset, set);

	return __xn_safe_copy_from_user(u_cset, &cset, sizeof(cset));
}

static int sys32_get_sigval(union sigval *val, const union compat_sigval *u_cval)
{
	union compat_sigval cval;
	int ret;

	if (u_cval == NULL)
		return -EFAULT;

	ret = __xn_safe_copy_from_user(&cval, u_cval, sizeof(cval));
	if (ret)
		return ret;

	val->sival_ptr = compat_ptr(cval.sival_ptr);

	return 0;
}

static int sys32_put_siginfo(void __user *u_si, const struct siginfo *si,
			     int overrun)
{
	struct compat_siginfo __user *u_p = u_si;
	int code, ret;

	if (u_p == NULL)
		return -EFAULT;

	/* Translate kernel codes for userland. */
	code = si->si_code;
	if (code & __SI_MASK)
		code |= __SI_MASK;

	ret = __xn_put_user(si->si_signo, &u_p->si_signo);
	ret |= __xn_put_user(si->si_errno, &u_p->si_errno);
	ret |= __xn_put_user(code, &u_p->si_code);

	/*
	 * Copy the generic/standard siginfo bits to userland.
	 */
	switch (si->si_code) {
	case SI_TIMER:
		ret |= __xn_put_user(si->si_tid, &u_p->si_tid);
		ret |= __xn_put_user(ptr_to_compat(si->si_ptr), &u_p->si_ptr);
		ret |= __xn_put_user(overrun, &u_p->si_overrun);
		break;
	case SI_QUEUE:
	case SI_MESGQ:
		ret |= __xn_put_user(ptr_to_compat(si->si_ptr), &u_p->si_ptr);
		/* falldown wanted. */
	case SI_USER:
		ret |= __xn_put_user(si->si_pid, &u_p->si_pid);
		ret |= __xn_put_user(si->si_uid, &u_p->si_uid);
	}

	return ret;
}

static int sys32_get_msghdr(struct msghdr *msg,
			    const struct compat_msghdr __user *u_cmsg)
{
	compat_uptr_t tmp1, tmp2, tmp3;

	if (u_cmsg == NULL ||
	    !access_rok(u_cmsg, sizeof(*u_cmsg)) ||
	    __xn_get_user(tmp1, &u_cmsg->msg_name) ||
	    __xn_get_user(msg->msg_namelen, &u_cmsg->msg_namelen) ||
	    __xn_get_user(tmp2, &u_cmsg->msg_iov) ||
	    __xn_get_user(msg->msg_iovlen, &u_cmsg->msg_iovlen) ||
	    __xn_get_user(tmp3, &u_cmsg->msg_control) ||
	    __xn_get_user(msg->msg_controllen, &u_cmsg->msg_controllen) ||
	    __xn_get_user(msg->msg_flags, &u_cmsg->msg_flags))
		return -EFAULT;

	if (msg->msg_namelen > sizeof(struct sockaddr_storage))
		msg->msg_namelen = sizeof(struct sockaddr_storage);

	msg->msg_name = compat_ptr(tmp1);
	msg->msg_iov = compat_ptr(tmp2);
	msg->msg_control = compat_ptr(tmp3);

	return 0;
}

static int sys32_put_msghdr(struct compat_msghdr __user *u_cmsg,
			    const struct msghdr *msg)
{
	if (u_cmsg == NULL ||
	    !access_wok(u_cmsg, sizeof(*u_cmsg)) ||
	    __xn_put_user(ptr_to_compat(msg->msg_name), &u_cmsg->msg_name) ||
	    __xn_put_user(msg->msg_namelen, &u_cmsg->msg_namelen) ||
	    __xn_put_user(ptr_to_compat(msg->msg_iov), &u_cmsg->msg_iov) ||
	    __xn_put_user(msg->msg_iovlen, &u_cmsg->msg_iovlen) ||
	    __xn_put_user(ptr_to_compat(msg->msg_control), &u_cmsg->msg_control) ||
	    __xn_put_user(msg->msg_controllen, &u_cmsg->msg_controllen) ||
	    __xn_put_user(msg->msg_flags, &u_cmsg->msg_flags))
		return -EFAULT;

	return 0;
}

COBALT_SYSCALL32emu(thread_create, init,
		    int, (compat_ulong_t pth,
			  int policy,
			  const struct compat_sched_param_ex __user *u_param_ex,
			  int xid,
			  __u32 __user *u_winoff))
{
	struct sched_param_ex param_ex;
	int ret;

	ret = sys32_get_param_ex(policy, &param_ex, u_param_ex);
	if (ret)
		return ret;

	return __cobalt_thread_create(pth, policy, &param_ex, xid, u_winoff);
}

COBALT_SYSCALL32emu(thread_setschedparam_ex, conforming,
		    int, (compat_ulong_t pth,
			  int policy,
			  const struct compat_sched_param_ex __user *u_param_ex,
			  __u32 __user *u_winoff,
			  int __user *u_promoted))
{
	struct sched_param_ex param_ex;
	int ret;

	ret = sys32_get_param_ex(policy, &param_ex, u_param_ex);
	if (ret)
		return ret;

	return __cobalt_thread_setschedparam_ex(pth, policy, &param_ex,
						u_winoff, u_promoted);
}

COBALT_SYSCALL32emu(thread_getschedparam_ex, current,
		    int, (compat_ulong_t pth,
			  int __user *u_policy,
			  struct compat_sched_param_ex __user *u_param))
{
	struct sched_param_ex param_ex;
	int policy;

	policy = __cobalt_thread_getschedparam_ex(pth, u_policy, &param_ex);
	if (policy < 0)
		return policy;

	return sys32_put_param_ex(policy, u_param, &param_ex);
}

static inline int sys32_fetch_timeout(struct timespec *ts,
				      const void __user *u_ts)
{
	return u_ts == NULL ? -EFAULT :
		sys32_get_timespec(ts, u_ts);
}

COBALT_SYSCALL32emu(sem_open, current,
		    int, (compat_uptr_t __user *u_addrp,
			  const char __user *u_name,
			  int oflags, mode_t mode, unsigned int value))
{
	struct cobalt_sem_shadow __user *usm;
	compat_uptr_t cusm;

	if (__xn_get_user(cusm, u_addrp))
		return -EFAULT;

	usm = __cobalt_sem_open(compat_ptr(cusm), u_name, oflags, mode, value);
	if (IS_ERR(usm))
		return PTR_ERR(usm);

	return __xn_put_user(ptr_to_compat(usm), u_addrp) ? -EFAULT : 0;
}

COBALT_SYSCALL32emu(sem_timedwait, primary,
		    int, (struct cobalt_sem_shadow __user *u_sem,
			  struct compat_timespec __user *u_ts))
{
	struct timespec ts;
	int ret;

	ret = sys32_get_timespec(&ts, u_ts);
	if (ret)
		return ret;

	return __cobalt_sem_timedwait(u_sem, u_ts, sys32_fetch_timeout);
}

COBALT_SYSCALL32emu(clock_getres, current,
		    int, (clockid_t clock_id,
			  struct compat_timespec __user *u_ts))
{
	struct timespec ts;
	int ret;

	ret = __cobalt_clock_getres(clock_id, &ts);
	if (ret)
		return ret;

	return sys32_put_timespec(u_ts, &ts);
}

COBALT_SYSCALL32emu(clock_gettime, current,
		    int, (clockid_t clock_id,
			  struct compat_timespec __user *u_ts))
{
	struct timespec ts;
	int ret;

	ret = __cobalt_clock_gettime(clock_id, &ts);
	if (ret)
		return ret;

	return sys32_put_timespec(u_ts, &ts);
}

COBALT_SYSCALL32emu(clock_settime, current,
		    int, (clockid_t clock_id,
			  const struct compat_timespec __user *u_ts))
{
	struct timespec ts;
	int ret;

	ret = sys32_get_timespec(&ts, u_ts);
	if (ret)
		return ret;

	return __cobalt_clock_settime(clock_id, &ts);
}

COBALT_SYSCALL32emu(clock_nanosleep, nonrestartable,
		    int, (clockid_t clock_id, int flags,
			  const struct compat_timespec __user *u_rqt,
			  struct compat_timespec __user *u_rmt))
{
	struct timespec rqt, rmt, *rmtp = NULL;
	int ret;

	if (u_rmt)
		rmtp = &rmt;

	ret = sys32_get_timespec(&rqt, u_rqt);
	if (ret)
		return ret;

	ret = __cobalt_clock_nanosleep(clock_id, flags, &rqt, rmtp);
	if (ret == -EINTR && flags == 0 && rmtp)
		ret = sys32_put_timespec(u_rmt, rmtp);

	return ret;
}

COBALT_SYSCALL32emu(mutex_timedlock, primary,
		    int, (struct cobalt_mutex_shadow __user *u_mx,
			  const struct compat_timespec __user *u_ts))
{
	return __cobalt_mutex_timedlock_break(u_mx, u_ts, sys32_fetch_timeout);
}

COBALT_SYSCALL32emu(cond_wait_prologue, nonrestartable,
		    int, (struct cobalt_cond_shadow __user *u_cnd,
			  struct cobalt_mutex_shadow __user *u_mx,
			  int *u_err,
			  unsigned int timed,
			  struct compat_timespec __user *u_ts))
{
	return __cobalt_cond_wait_prologue(u_cnd, u_mx, u_err, u_ts,
					   timed ? sys32_fetch_timeout : NULL);
}

COBALT_SYSCALL32emu(mq_open, lostage,
		    int, (const char __user *u_name, int oflags,
			  mode_t mode, struct compat_mq_attr __user *u_attr))
{
	struct mq_attr _attr, *attr = &_attr;
	int ret;

	if ((oflags & O_CREAT) && u_attr) {
		ret = sys32_get_mqattr(&_attr, u_attr);
		if (ret)
			return ret;
	} else
		attr = NULL;

	return __cobalt_mq_open(u_name, oflags, mode, attr);
}

COBALT_SYSCALL32emu(mq_getattr, current,
		    int, (mqd_t uqd, struct compat_mq_attr __user *u_attr))
{
	struct mq_attr attr;
	int ret;

	ret = __cobalt_mq_getattr(uqd, &attr);
	if (ret)
		return ret;

	return sys32_put_mqattr(u_attr, &attr);
}

COBALT_SYSCALL32emu(mq_setattr, current,
		    int, (mqd_t uqd, const struct compat_mq_attr __user *u_attr,
			  struct compat_mq_attr __user *u_oattr))
{
	struct mq_attr attr, oattr;
	int ret;

	ret = sys32_get_mqattr(&attr, u_attr);
	if (ret)
		return ret;

	ret = __cobalt_mq_setattr(uqd, &attr, &oattr);
	if (ret)
		return ret;

	if (u_oattr == NULL)
		return 0;

	return sys32_put_mqattr(u_oattr, &oattr);
}

COBALT_SYSCALL32emu(mq_timedsend, primary,
		    int, (mqd_t uqd, const void __user *u_buf, size_t len,
			  unsigned int prio,
			  const struct compat_timespec __user *u_ts))
{
	return __cobalt_mq_timedsend(uqd, u_buf, len, prio,
				     u_ts, u_ts ? sys32_fetch_timeout : NULL);
}

COBALT_SYSCALL32emu(mq_timedreceive, primary,
		    int, (mqd_t uqd, void __user *u_buf,
			  compat_ssize_t __user *u_len,
			  unsigned int __user *u_prio,
			  const struct compat_timespec __user *u_ts))
{
	compat_ssize_t clen;
	ssize_t len;
	int ret;

	ret = __cobalt_mq_timedreceive(uqd, u_buf, &len, u_prio,
				       u_ts, u_ts ? sys32_fetch_timeout : NULL);
	if (ret)
		return ret;

	clen = len;

	return __xn_safe_copy_to_user(u_len, &clen, sizeof(*u_len));
}

COBALT_SYSCALL32emu(mq_notify, primary,
		    int, (mqd_t fd, const struct compat_sigevent *__user u_cev))
{
	struct sigevent sev;
	int ret;

	if (u_cev) {
		ret = sys32_get_sigevent(&sev, u_cev);
		if (ret)
			return ret;
	}

	return __cobalt_mq_notify(fd, u_cev ? &sev : NULL);
}

COBALT_SYSCALL32emu(sched_weightprio, current,
		    int, (int policy,
			  const struct compat_sched_param_ex __user *u_param))
{
	struct sched_param_ex param_ex;
	int ret;

	ret = sys32_get_param_ex(policy, &param_ex, u_param);
	if (ret)
		return ret;

	return __cobalt_sched_weightprio(policy, &param_ex);
}

static union sched_config *
sys32_fetch_config(int policy, const void __user *u_config, size_t *len)
{
	union compat_sched_config *cbuf;
	union sched_config *buf;
	int ret, n;

	if (u_config == NULL)
		return ERR_PTR(-EFAULT);

	if (policy == SCHED_QUOTA && *len < sizeof(cbuf->quota))
		return ERR_PTR(-EINVAL);

	cbuf = xnmalloc(*len);
	if (cbuf == NULL)
		return ERR_PTR(-ENOMEM);

	ret = __xn_safe_copy_from_user(cbuf, u_config, *len);
	if (ret) {
		buf = ERR_PTR(ret);
		goto out;
	}

	switch (policy) {
	case SCHED_TP:
		*len = sched_tp_confsz(cbuf->tp.nr_windows);
		break;
	case SCHED_QUOTA:
		break;
	default:
		buf = ERR_PTR(-EINVAL);
		goto out;
	}

	buf = xnmalloc(*len);
	if (buf == NULL) {
		buf = ERR_PTR(-ENOMEM);
		goto out;
	}

	if (policy == SCHED_QUOTA)
		memcpy(&buf->quota, &cbuf->quota, sizeof(cbuf->quota));
	else {
		buf->tp.nr_windows = cbuf->tp.nr_windows;
		for (n = 0; n < buf->tp.nr_windows; n++) {
			buf->tp.windows[n].ptid = cbuf->tp.windows[n].ptid;
			buf->tp.windows[n].offset.tv_sec = cbuf->tp.windows[n].offset.tv_sec;
			buf->tp.windows[n].offset.tv_nsec = cbuf->tp.windows[n].offset.tv_nsec;
			buf->tp.windows[n].duration.tv_sec = cbuf->tp.windows[n].duration.tv_sec;
			buf->tp.windows[n].duration.tv_nsec = cbuf->tp.windows[n].duration.tv_nsec;
		}
	}
out:
	xnfree(cbuf);

	return buf;
}

static int sys32_ack_config(int policy, const union sched_config *config,
			    void __user *u_config)
{
	union compat_sched_config __user *u_p = u_config;

	if (policy != SCHED_QUOTA)
		return 0;

	return u_config == NULL ? -EFAULT :
		__xn_safe_copy_to_user(&u_p->quota.info, &config->quota.info,
				       sizeof(u_p->quota.info));
}

static ssize_t sys32_put_config(int policy,
				void __user *u_config, size_t u_len,
				const union sched_config *config, size_t len)
{
	union compat_sched_config __user *u_p = u_config;
	int n, ret;

	if (u_config == NULL)
		return -EFAULT;

	if (policy == SCHED_QUOTA) {
		if (u_len < sizeof(u_p->quota))
			return -EINVAL;
		return __xn_safe_copy_to_user(&u_p->quota.info, &config->quota.info,
					      sizeof(u_p->quota.info)) ?:
			sizeof(u_p->quota.info);
	}

	if (u_len < compat_sched_tp_confsz(config->tp.nr_windows))
		return -ENOSPC;

	__xn_put_user(config->tp.nr_windows, &u_p->tp.nr_windows);

	for (n = 0, ret = 0; n < config->tp.nr_windows; n++) {
		ret |= __xn_put_user(config->tp.windows[n].ptid,
				     &u_p->tp.windows[n].ptid);
		ret |= __xn_put_user(config->tp.windows[n].offset.tv_sec,
				     &u_p->tp.windows[n].offset.tv_sec);
		ret |= __xn_put_user(config->tp.windows[n].offset.tv_nsec,
				     &u_p->tp.windows[n].offset.tv_nsec);
		ret |= __xn_put_user(config->tp.windows[n].duration.tv_sec,
				     &u_p->tp.windows[n].duration.tv_sec);
		ret |= __xn_put_user(config->tp.windows[n].duration.tv_nsec,
				     &u_p->tp.windows[n].duration.tv_nsec);
	}

	return ret ?: u_len;
}

COBALT_SYSCALL32emu(sched_setconfig_np, current,
		    int, (int cpu, int policy,
			  union compat_sched_config __user *u_config,
			  size_t len))
{
	return __cobalt_sched_setconfig_np(cpu, policy, u_config, len,
					   sys32_fetch_config, sys32_ack_config);
}

COBALT_SYSCALL32emu(sched_getconfig_np, current,
		    ssize_t, (int cpu, int policy,
			      union compat_sched_config __user *u_config,
			      size_t len))
{
	return __cobalt_sched_getconfig_np(cpu, policy, u_config, len,
					   sys32_fetch_config, sys32_put_config);
}

COBALT_SYSCALL32emu(timer_create, current,
		    int, (clockid_t clock,
			  const struct compat_sigevent __user *u_sev,
			  timer_t __user *u_tm))
{
	struct sigevent sev, *evp = NULL;
	int ret;

	if (u_sev) {
		evp = &sev;
		ret = sys32_get_sigevent(&sev, u_sev);
		if (ret)
			return ret;
	}

	return __cobalt_timer_create(clock, evp, u_tm);
}

COBALT_SYSCALL32emu(timer_settime, primary,
		    int, (timer_t tm, int flags,
			  const struct compat_itimerspec __user *u_newval,
			  struct compat_itimerspec __user *u_oldval))
{
	struct itimerspec newv, oldv, *oldvp = &oldv;
	int ret;

	if (u_oldval == NULL)
		oldvp = NULL;

	ret = sys32_get_itimerspec(&newv, u_newval);
	if (ret)
		return ret;

	ret = __cobalt_timer_settime(tm, flags, &newv, oldvp);
	if (ret)
		return ret;

	if (oldvp) {
		ret = sys32_put_itimerspec(u_oldval, oldvp);
		if (ret)
			__cobalt_timer_settime(tm, flags, oldvp, NULL);
	}

	return ret;
}

COBALT_SYSCALL32emu(timer_gettime, current,
		    int, (timer_t tm, struct compat_itimerspec __user *u_val))
{
	struct itimerspec val;
	int ret;

	ret = __cobalt_timer_gettime(tm, &val);

	return ret ?: sys32_put_itimerspec(u_val, &val);
}

COBALT_SYSCALL32emu(timerfd_settime, primary,
		    int, (int fd, int flags,
			  const struct compat_itimerspec __user *new_value,
			  struct compat_itimerspec __user *old_value))
{
	struct itimerspec ovalue, value;
	int ret;

	ret = sys32_get_itimerspec(&value, new_value);
	if (ret)
		return ret;

	ret = __cobalt_timerfd_settime(fd, flags, &value, &ovalue);
	if (ret)
		return ret;

	if (old_value) {
		ret = sys32_put_itimerspec(old_value, &ovalue);
		value.it_value.tv_sec = 0;
		value.it_value.tv_nsec = 0;
		__cobalt_timerfd_settime(fd, flags, &value, NULL);
	}

	return ret;
}

COBALT_SYSCALL32emu(timerfd_gettime, current,
		    int, (int fd, struct compat_itimerspec __user *curr_value))
{
	struct itimerspec value;
	int ret;

	ret = __cobalt_timerfd_gettime(fd, &value);

	return ret ?: sys32_put_itimerspec(curr_value, &value);
}

COBALT_SYSCALL32emu(sigwait, primary,
		    int, (const compat_sigset_t __user *u_set,
			  int __user *u_sig))
{
	sigset_t set;
	int ret, sig;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	sig = __cobalt_sigwait(&set);
	if (sig < 0)
		return sig;

	return __xn_safe_copy_to_user(u_sig, &sig, sizeof(*u_sig));
}

COBALT_SYSCALL32emu(sigtimedwait, nonrestartable,
		    int, (const compat_sigset_t __user *u_set,
			  struct compat_siginfo __user *u_si,
			  const struct compat_timespec __user *u_timeout))
{
	struct timespec timeout;
	sigset_t set;
	int ret;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	ret = sys32_get_timespec(&timeout, u_timeout);
	if (ret)
		return ret;

	return __cobalt_sigtimedwait(&set, &timeout, u_si, sys32_put_siginfo);
}

COBALT_SYSCALL32emu(sigwaitinfo, nonrestartable,
		    int, (const compat_sigset_t __user *u_set,
			  struct compat_siginfo __user *u_si))
{
	sigset_t set;
	int ret;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	return __cobalt_sigwaitinfo(&set, u_si, sys32_put_siginfo);
}

COBALT_SYSCALL32emu(sigpending, primary, int, (compat_sigset_t __user *u_set))
{
	struct cobalt_thread *curr = cobalt_current_thread();
	
	return sys32_put_sigset(u_set, &curr->sigpending);
}

COBALT_SYSCALL32emu(sigqueue, conforming,
		    int, (pid_t pid, int sig,
			  const union compat_sigval __user *u_value))
{
	union sigval val;
	int ret;

	ret = sys32_get_sigval(&val, u_value);

	return ret ?: __cobalt_sigqueue(pid, sig, &val);
}

COBALT_SYSCALL32emu(monitor_wait, nonrestartable,
		    int, (struct cobalt_monitor_shadow __user *u_mon,
			  int event, const struct compat_timespec __user *u_ts,
			  int __user *u_ret))
{
	struct timespec ts, *tsp = NULL;
	int ret;

	if (u_ts) {
		tsp = &ts;
		ret = sys32_get_timespec(&ts, u_ts);
		if (ret)
			return ret;
	}

	return __cobalt_monitor_wait(u_mon, event, tsp, u_ret);
}

COBALT_SYSCALL32emu(event_wait, primary,
		    int, (struct cobalt_event_shadow __user *u_event,
			  unsigned int bits,
			  unsigned int __user *u_bits_r,
			  int mode, const struct compat_timespec __user *u_ts))
{
	struct timespec ts, *tsp = NULL;
	int ret;

	if (u_ts) {
		tsp = &ts;
		ret = sys32_get_timespec(&ts, u_ts);
		if (ret)
			return ret;
	}

	return __cobalt_event_wait(u_event, bits, u_bits_r, mode, tsp);
}

COBALT_SYSCALL32emu(select, nonrestartable,
		    int, (int nfds,
			  compat_fd_set __user *u_rfds,
			  compat_fd_set __user *u_wfds,
			  compat_fd_set __user *u_xfds,
			  struct compat_timeval __user *u_tv))
{
	compat_fd_set __user *ufd_sets[XNSELECT_MAX_TYPES] = {
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
	struct xnthread *curr;
	struct timeval tv;
	xnsticks_t diff;
	size_t fds_size;
	int i, err;

	curr = xnthread_current();

	if (u_tv) {
		err = sys32_get_timeval(&tv, u_tv);
		if (err)
			return err;

		if (tv.tv_usec > 1000000)
			return -EINVAL;

		timeout = clock_get_ticks(CLOCK_MONOTONIC) + tv2ns(&tv);
		mode = XN_ABSOLUTE;
	}

	fds_size = __FDELT__(nfds + __NFDBITS__ - 1) * sizeof(compat_ulong_t);

	for (i = 0; i < XNSELECT_MAX_TYPES; i++)
		if (ufd_sets[i]) {
			in_fds[i] = &in_fds_storage[i];
			out_fds[i] = & out_fds_storage[i];
			if (sys32_get_fdset(in_fds[i], ufd_sets[i], fds_size) < 0)
				return -EFAULT;
		}

	selector = curr->selector;
	if (selector == NULL) {
		/* Bail out if non-RTDM fildes is found. */
		if (!__cobalt_first_fd_valid_p(in_fds, nfds))
			return -EBADF;

		selector = xnmalloc(sizeof(*curr->selector));
		if (selector == NULL)
			return -ENOMEM;
		xnselector_init(selector);
		curr->selector = selector;

		/* Bind directly the file descriptors, we do not need to go
		   through xnselect returning -ECHRNG */
		err = __cobalt_select_bind_all(selector, in_fds, nfds);
		if (err)
			return err;
	}

	do {
		err = xnselect(selector, out_fds, in_fds, nfds, timeout, mode);
		if (err == -ECHRNG) {
			int err = __cobalt_select_bind_all(selector, out_fds, nfds);
			if (err)
				return err;
		}
	} while (err == -ECHRNG);

	if (u_tv && (err > 0 || err == -EINTR)) {
		diff = timeout - clock_get_ticks(CLOCK_MONOTONIC);
		if (diff > 0)
			ticks2tv(&tv, diff);
		else
			tv.tv_sec = tv.tv_usec = 0;

		if (sys32_put_timeval(u_tv, &tv))
			return -EFAULT;
	}

	if (err >= 0)
		for (i = 0; i < XNSELECT_MAX_TYPES; i++)
			if (ufd_sets[i] &&
			    sys32_put_fdset(ufd_sets[i], out_fds[i],
					    sizeof(fd_set)) < 0)
				return -EFAULT;
	return err;
}

COBALT_SYSCALL32emu(recvmsg, probing,
		    ssize_t, (int fd, struct compat_msghdr __user *umsg,
			      int flags))
{
	struct msghdr m;
	ssize_t ret;

	ret = sys32_get_msghdr(&m, umsg);
	if (ret)
		return ret;

	ret = rtdm_fd_recvmsg(fd, &m, flags);
	if (ret < 0)
		return ret;

	return sys32_put_msghdr(umsg, &m) ?: ret;
}

COBALT_SYSCALL32emu(sendmsg, probing,
		    ssize_t, (int fd, struct compat_msghdr __user *umsg,
			      int flags))
{
	struct msghdr m;
	int ret;

	ret = sys32_get_msghdr(&m, umsg);

	return ret ?: rtdm_fd_sendmsg(fd, &m, flags);
}

COBALT_SYSCALL32emu(mmap, lostage,
		    int, (int fd, struct compat_rtdm_mmap_request __user *u_crma,
			  compat_uptr_t __user *u_caddrp))
{
	struct _rtdm_mmap_request rma;
	compat_uptr_t u_caddr;
	void *u_addr = NULL;
	int ret;

	if (u_crma == NULL ||
	    !access_rok(u_crma, sizeof(*u_crma)) ||
	    __xn_get_user(rma.length, &u_crma->length) ||
	    __xn_get_user(rma.offset, &u_crma->offset) ||
	    __xn_get_user(rma.prot, &u_crma->prot) ||
	    __xn_get_user(rma.flags, &u_crma->flags))
	  return -EFAULT;

	ret = rtdm_fd_mmap(fd, &rma, &u_addr);
	if (ret)
		return ret;

	u_caddr = ptr_to_compat(u_addr);

	return __xn_safe_copy_to_user(u_caddrp, &u_caddr, sizeof(u_caddr));
}

COBALT_SYSCALL32emu(backtrace, current,
		    int, (int nr, compat_ulong_t __user *u_backtrace,
			  int reason))
{
	compat_ulong_t cbacktrace[SIGSHADOW_BACKTRACE_DEPTH];
	unsigned long backtrace[SIGSHADOW_BACKTRACE_DEPTH];
	int ret, n;

	if (nr <= 0)
		return 0;

	if (nr > SIGSHADOW_BACKTRACE_DEPTH)
		nr = SIGSHADOW_BACKTRACE_DEPTH;

	ret = __xn_safe_copy_from_user(cbacktrace, u_backtrace,
				       nr * sizeof(compat_ulong_t));
	if (ret)
		return ret;

	for (n = 0; n < nr; n++)
		backtrace [n] = cbacktrace[n];

	xndebug_trace_relax(nr, backtrace, reason);

	return 0;
}
