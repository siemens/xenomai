/*
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
#include <linux/ipipe.h>
#include <linux/kconfig.h>
#include <cobalt/uapi/syscall.h>
#include <cobalt/uapi/sysconf.h>
#include <cobalt/kernel/tree.h>
#include <cobalt/kernel/vdso.h>
#include <xenomai/version.h>
#include <asm-generic/xenomai/mayday.h>
#include "internal.h"
#include "thread.h"
#include "sched.h"
#include "mutex.h"
#include "cond.h"
#include "mqueue.h"
#include "sem.h"
#include "signal.h"
#include "timer.h"
#include "monitor.h"
#include "clock.h"
#include "event.h"
#include "timerfd.h"
#include "io.h"
#include "../debug.h"
#include <trace/events/cobalt-posix.h>

/* Syscall must run into the Linux domain. */
#define __xn_exec_lostage    0x1
/* Syscall must run into the Xenomai domain. */
#define __xn_exec_histage    0x2
/* Shadow syscall: caller must be mapped. */
#define __xn_exec_shadow     0x4
/* Switch back toggle; caller must return to its original mode. */
#define __xn_exec_switchback 0x8
/* Exec in current domain. */
#define __xn_exec_current    0x10
/* Exec in conforming domain, Xenomai or Linux. */
#define __xn_exec_conforming 0x20
/* Attempt syscall restart in the opposite domain upon -ENOSYS. */
#define __xn_exec_adaptive   0x40
/* Do not restart syscall upon signal receipt. */
#define __xn_exec_norestart  0x80
/* Shorthand for shadow init syscall. */
#define __xn_exec_init       __xn_exec_lostage
/* Shorthand for shadow syscall in Xenomai space. */
#define __xn_exec_primary   (__xn_exec_shadow|__xn_exec_histage)
/* Shorthand for shadow syscall in Linux space. */
#define __xn_exec_secondary (__xn_exec_shadow|__xn_exec_lostage)
/* Shorthand for syscall in Linux space with switchback if shadow. */
#define __xn_exec_downup    (__xn_exec_lostage|__xn_exec_switchback)
/* Shorthand for non-restartable primary syscall. */
#define __xn_exec_nonrestartable (__xn_exec_primary|__xn_exec_norestart)
/* Shorthand for domain probing syscall */
#define __xn_exec_probing   (__xn_exec_conforming|__xn_exec_adaptive)
/* Shorthand for oneway trap - does not return to call site. */
#define __xn_exec_oneway    __xn_exec_norestart

static struct cobalt_syscall {
	int (*handler)(unsigned long arg1, unsigned long arg2,
		       unsigned long arg3, unsigned long arg4,
		       unsigned long arg5);
	int flags;
} cobalt_syscalls[];

static void prepare_for_signal(struct task_struct *p,
			       struct xnthread *thread,
			       struct pt_regs *regs,
			       int sysflags)
{
	int notify = 0;

	if (xnthread_test_info(thread, XNKICKED)) {
		if (signal_pending(p)) {
			__xn_error_return(regs,
					  (sysflags & __xn_exec_norestart) ?
					  -EINTR : -ERESTARTSYS);
			notify = !xnthread_test_state(thread, XNDEBUG);
			xnthread_clear_info(thread, XNBREAK);
		}
		xnthread_clear_info(thread, XNKICKED);
	}

	xnthread_test_cancel();

	xnthread_relax(notify, SIGDEBUG_MIGRATE_SIGNAL);
}

static int handle_head_syscall(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	struct cobalt_process *process;
	int nr, switched, ret, sigs;
	struct cobalt_syscall *sc;
	struct xnthread *thread;
	unsigned long sysflags;
	struct task_struct *p;

	thread = xnthread_current();
	if (thread)
		thread->regs = regs;

	if (!__xn_syscall_p(regs))
		goto linux_syscall;

	nr = __xn_syscall(regs);

	trace_cobalt_head_sysentry(thread, nr);

	if (nr < 0 || nr >= __NR_COBALT_SYSCALLS)
		goto bad_syscall;

	process = cobalt_current_process();
	if (process == NULL) {
		process = cobalt_search_process(current->mm);
		cobalt_set_process(process);
	}

	sc = cobalt_syscalls + nr;
	sysflags = sc->flags;

	/*
	 * Executing Cobalt services requires CAP_SYS_NICE, except for
	 * sc_cobalt_bind which does its own checks.
	 */
	if (unlikely((process == NULL && nr != sc_cobalt_bind) ||
		     (thread == NULL && (sysflags & __xn_exec_shadow) != 0) ||
		     (!cap_raised(current_cap(), CAP_SYS_NICE) &&
		      nr != sc_cobalt_bind))) {
		if (XENO_DEBUG(COBALT))
			printk(XENO_WARN
			       "syscall <%d> denied to %s[%d]\n",
			       nr, current->comm, current->pid);
		__xn_error_return(regs, -EPERM);
		goto ret_handled;
	}

	if (sysflags & __xn_exec_conforming)
		/*
		 * If the conforming exec bit is set, turn the exec
		 * bitmask for the syscall into the most appropriate
		 * setup for the caller, i.e. Xenomai domain for
		 * shadow threads, Linux otherwise.
		 */
		sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);

	/*
	 * Here we have to dispatch the syscall execution properly,
	 * depending on:
	 *
	 * o Whether the syscall must be run into the Linux or Xenomai
	 * domain, or indifferently in the current Xenomai domain.
	 *
	 * o Whether the caller currently runs in the Linux or Xenomai
	 * domain.
	 */
	switched = 0;
restart:
	/*
	 * Process adaptive syscalls by restarting them in the
	 * opposite domain.
	 */
	if (sysflags & __xn_exec_lostage) {
		/*
		 * The syscall must run from the Linux domain.
		 */
		if (ipd == &xnsched_realtime_domain) {
			/*
			 * Request originates from the Xenomai domain:
			 * relax the caller then invoke the syscall
			 * handler right after.
			 */
			xnthread_relax(1, SIGDEBUG_MIGRATE_SYSCALL);
			switched = 1;
		} else
			/*
			 * Request originates from the Linux domain:
			 * propagate the event to our Linux-based
			 * handler, so that the syscall is executed
			 * from there.
			 */
			return KEVENT_PROPAGATE;
	} else if (sysflags & (__xn_exec_histage | __xn_exec_current)) {
		/*
		 * Syscall must run either from the Xenomai domain, or
		 * from the calling domain.
		 *
		 * If the request originates from the Linux domain,
		 * hand it over to our secondary-mode dispatcher.
		 * Otherwise, invoke the syscall handler immediately.
		 */
		if (ipd != &xnsched_realtime_domain)
			return KEVENT_PROPAGATE;
	}

	ret = sc->handler(__xn_reg_arglist(regs));
	if (ret == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
		if (switched) {
			switched = 0;
			ret = xnthread_harden();
			if (ret)
				goto done;
		}

		sysflags ^=
		    (__xn_exec_lostage | __xn_exec_histage |
		     __xn_exec_adaptive);
		goto restart;
	}
done:
	__xn_status_return(regs, ret);
	sigs = 0;
	if (!xnsched_root_p()) {
		p = current;
		if (signal_pending(p) ||
		    xnthread_test_info(thread, XNKICKED)) {
			sigs = 1;
			prepare_for_signal(p, thread, regs, sysflags);
		} else if (xnthread_test_state(thread, XNWEAK) &&
			   xnthread_get_rescnt(thread) == 0) {
			if (switched)
				switched = 0;
			else
				xnthread_relax(0, 0);
		}
	}
	if (!sigs && (sysflags & __xn_exec_switchback) != 0 && switched)
		xnthread_harden(); /* -EPERM will be trapped later if needed. */

ret_handled:
	/* Update the stats and userland-visible state. */
	if (thread) {
		xnstat_counter_inc(&thread->stat.xsc);
		xnthread_sync_window(thread);
	}

	trace_cobalt_head_sysexit(thread, __xn_reg_rval(regs));

	return KEVENT_STOP;

linux_syscall:
	if (xnsched_root_p())
		/*
		 * The call originates from the Linux domain, either
		 * from a relaxed shadow or from a regular Linux task;
		 * just propagate the event so that we will fall back
		 * to handle_root_syscall().
		 */
		return KEVENT_PROPAGATE;

	/*
	 * From now on, we know that we have a valid shadow thread
	 * pointer.
	 *
	 * The current syscall will eventually fall back to the Linux
	 * syscall handler if our Linux domain handler does not
	 * intercept it. Before we let it go, ensure that the current
	 * thread has properly entered the Linux domain.
	 */
	xnthread_relax(1, SIGDEBUG_MIGRATE_SYSCALL);

	return KEVENT_PROPAGATE;

bad_syscall:
	printk(XENO_WARN "bad syscall <%ld>\n", __xn_syscall(regs));
	
	__xn_error_return(regs, -ENOSYS);

	return KEVENT_STOP;
}

static int handle_root_syscall(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	int nr, sysflags, switched, ret, sigs;
	struct cobalt_syscall *sc;
	struct xnthread *thread;
	struct task_struct *p;

	/*
	 * Catch cancellation requests pending for user shadows
	 * running mostly in secondary mode, i.e. XNWEAK. In that
	 * case, we won't run prepare_for_signal() that frequently, so
	 * check for cancellation here.
	 */
	xnthread_test_cancel();

	thread = xnthread_current();
	if (thread)
		thread->regs = regs;

	if (!__xn_syscall_p(regs))
		/* Fall back to Linux syscall handling. */
		return KEVENT_PROPAGATE;

	/* nr has already been checked in the head domain handler. */
	nr = __xn_syscall(regs);

	trace_cobalt_root_sysentry(thread, nr);

	/* Processing a Xenomai syscall. */

	sc = cobalt_syscalls + nr;
	sysflags = sc->flags;

	if ((sysflags & __xn_exec_conforming) != 0)
		sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);
restart:
	/*
	 * Process adaptive syscalls by restarting them in the
	 * opposite domain.
	 */
	if (sysflags & __xn_exec_histage) {
		/*
		 * This request originates from the Linux domain and
		 * must be run into the Xenomai domain: harden the
		 * caller and execute the syscall.
		 */
		ret = xnthread_harden();
		if (ret) {
			__xn_error_return(regs, ret);
			goto ret_handled;
		}
		switched = 1;
	} else
		/*
		 * We want to run the syscall in the Linux domain.
		 */
		switched = 0;

	ret = sc->handler(__xn_reg_arglist(regs));
	if (ret == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
		if (switched) {
			switched = 0;
			xnthread_relax(1, SIGDEBUG_MIGRATE_SYSCALL);
		}

		sysflags ^=
		    (__xn_exec_lostage | __xn_exec_histage |
		     __xn_exec_adaptive);
		goto restart;
	}

	__xn_status_return(regs, ret);

	sigs = 0;
	if (!xnsched_root_p()) {
		/*
		 * We may have gained a shadow TCB from the syscall we
		 * just invoked, so make sure to fetch it.
		 */
		thread = xnthread_current();
		p = current;
		if (signal_pending(p)) {
			sigs = 1;
			prepare_for_signal(p, thread, regs, sysflags);
		} else if (xnthread_test_state(thread, XNWEAK) &&
			   xnthread_get_rescnt(thread) == 0)
			sysflags |= __xn_exec_switchback;
	}
	if (!sigs && (sysflags & __xn_exec_switchback) != 0
	    && (switched || xnsched_primary_p()))
		xnthread_relax(0, 0);

ret_handled:
	/* Update the stats and userland-visible state. */
	if (thread) {
		xnstat_counter_inc(&thread->stat.xsc);
		xnthread_sync_window(thread);
	}

	trace_cobalt_root_sysexit(thread, __xn_reg_rval(regs));

	return KEVENT_STOP;
}

int ipipe_syscall_hook(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	if (unlikely(ipipe_root_p))
		return handle_root_syscall(ipd, regs);

	return handle_head_syscall(ipd, regs);
}

int ipipe_fastcall_hook(struct pt_regs *regs)
{
	int ret;

	ret = handle_head_syscall(&xnsched_realtime_domain, regs);
	XENO_BUGON(COBALT, ret == KEVENT_PROPAGATE);

	return ret;
}

static int cobalt_migrate(int domain)
{
	struct xnthread *thread = xnthread_current();

	if (ipipe_root_p) {
		if (domain == COBALT_PRIMARY) {
			if (thread == NULL)
				return -EPERM;
			/*
			 * Paranoid: a corner case where userland
			 * fiddles with SIGSHADOW while the target
			 * thread is still waiting to be started.
			 */
			if (xnthread_test_state(thread, XNDORMANT))
				return 0;

			return xnthread_harden() ? : 1;
		}
		return 0;
	}

	/* ipipe_current_domain != ipipe_root_domain */
	if (domain == COBALT_SECONDARY) {
		xnthread_relax(0, 0);
		return 1;
	}

	return 0;
}

static int cobalt_info(struct cobalt_sysinfo __user *u_info)
{
	struct cobalt_sysinfo info;

	info.clockfreq = xnarch_machdata.clock_freq;
	info.vdso = xnheap_mapped_offset(&cobalt_ppd_get(1)->sem_heap,
					 nkvdso);

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

static int cobalt_trace(int op, unsigned long a1,
			unsigned long a2, unsigned long a3)
{
	int ret = -EINVAL;

	switch (op) {
	case __xntrace_op_max_begin:
		ret = xntrace_max_begin(a1);
		break;

	case __xntrace_op_max_end:
		ret = xntrace_max_end(a1);
		break;

	case __xntrace_op_max_reset:
		ret = xntrace_max_reset();
		break;

	case __xntrace_op_user_start:
		ret = xntrace_user_start();
		break;

	case __xntrace_op_user_stop:
		ret = xntrace_user_stop(a1);
		break;

	case __xntrace_op_user_freeze:
		ret = xntrace_user_freeze(a1, a2);
		break;

	case __xntrace_op_special:
		ret = xntrace_special(a1 & 0xFF, a2);
		break;

	case __xntrace_op_special_u64:
		ret = xntrace_special_u64(a1 & 0xFF,
					  (((u64) a2) << 32) | a3);
		break;
	}
	return ret;
}

static int cobalt_heapstat(struct cobalt_heapstat __user *u_hd,
			   unsigned int heap_nr)
{
	struct cobalt_heapstat hd;
	struct xnheap *heap;

	switch(heap_nr) {
	case COBALT_PRIVATE_HEAP:
	case COBALT_SHARED_HEAP:
		heap = &cobalt_ppd_get(heap_nr)->sem_heap;
		break;
	case COBALT_GLOBAL_HEAP:
		heap = &kheap;
		break;
	default:
		return -EINVAL;
	}

	hd.handle = (unsigned long)heap;
	hd.size = xnheap_extentsize(heap);
	hd.area = xnheap_base_memory(heap);
	hd.used = xnheap_used_mem(heap);

	return __xn_safe_copy_to_user(u_hd, &hd, sizeof(*u_hd));
}

static int cobalt_get_current(xnhandle_t __user *u_handle)
{
	struct xnthread *cur = xnthread_current();

	if (cur == NULL)
		return -EPERM;

	return __xn_safe_copy_to_user(u_handle, &xnthread_handle(cur),
				      sizeof(*u_handle));
}

static int cobalt_backtrace(int nr, unsigned long __user *u_backtrace, int reason)
{
	xndebug_trace_relax(nr, u_backtrace, reason);
	return 0;
}

static int cobalt_serialdbg(const char __user *u_msg, int len)
{
	char buf[128];
	int n;

	while (len > 0) {
		n = len;
		if (n > sizeof(buf))
			n = sizeof(buf);
		if (__xn_safe_copy_from_user(buf, u_msg, n))
			return -EFAULT;
		__ipipe_serial_debug("%.*s", n, buf);
		u_msg += n;
		len -= n;
	}

	return 0;
}

static int cobalt_mayday(void)
{
	struct xnthread *cur;

	cur = xnthread_current();
	if (likely(cur)) {
		/*
		 * If the thread was kicked by the watchdog, this
		 * syscall we have just forced on it via the mayday
		 * escape will cause it to relax. See
		 * handle_head_syscall().
		 */
		xnarch_fixup_mayday(xnthread_archtcb(cur), cur->regs);

		/*
		 * Return whatever value xnarch_fixup_mayday set for
		 * this register, in order not to undo what
		 * xnarch_fixup_mayday did.
		 */
		return __xn_reg_rval(cur->regs);
	}

	printk(XENO_WARN "MAYDAY received from invalid context %s[%d]\n",
	       current->comm, current->pid);

	return -EPERM;
}

static void stringify_feature_set(unsigned long fset, char *buf, int size)
{
	unsigned long feature;
	int nc, nfeat;

	*buf = '\0';

	for (feature = 1, nc = nfeat = 0; fset != 0 && size > 0; feature <<= 1) {
		if (fset & feature) {
			nc = ksformat(buf, size, "%s%s",
				      nfeat > 0 ? " " : "",
				      get_feature_label(feature));
			nfeat++;
			size -= nc;
			buf += nc;
			fset &= ~feature;
		}
	}
}

static int cobalt_bind(struct cobalt_bindreq __user *u_breq)
{
	unsigned long featreq, featmis;
	struct cobalt_bindreq breq;
	struct cobalt_featinfo *f;
	int abirev;

	if (__xn_safe_copy_from_user(&breq, u_breq, sizeof(breq)))
		return -EFAULT;

	f = &breq.feat_ret;
	featreq = breq.feat_req;
	featmis = (~XENOMAI_FEAT_DEP & (featreq & XENOMAI_FEAT_MAN));
	abirev = breq.abi_rev;

	/*
	 * Pass back the supported feature set and the ABI revision
	 * level to user-space.
	 */
	f->feat_all = XENOMAI_FEAT_DEP;
	stringify_feature_set(XENOMAI_FEAT_DEP, f->feat_all_s,
			      sizeof(f->feat_all_s));
	f->feat_man = featreq & XENOMAI_FEAT_MAN;
	stringify_feature_set(f->feat_man, f->feat_man_s,
			      sizeof(f->feat_man_s));
	f->feat_mis = featmis;
	stringify_feature_set(featmis, f->feat_mis_s,
			      sizeof(f->feat_mis_s));
	f->feat_req = featreq;
	stringify_feature_set(featreq, f->feat_req_s,
			      sizeof(f->feat_req_s));
	f->feat_abirev = XENOMAI_ABI_REV;
	collect_arch_features(f);

	if (__xn_safe_copy_to_user(u_breq, &breq, sizeof(breq)))
		return -EFAULT;

	/*
	 * If some mandatory features the user-space code relies on
	 * are missing at kernel level, we cannot go further.
	 */
	if (featmis)
		return -EINVAL;

	if (!check_abi_revision(abirev))
		return -ENOEXEC;

	return cobalt_bind_core();
}

static int cobalt_extend(unsigned int magic)
{
	return cobalt_bind_personality(magic);
}

static int cobalt_sysconf(int option, void __user *u_buf, size_t u_bufsz)
{
	int ret, val = 0;

	if (u_bufsz < sizeof(val))
		return -EINVAL;

	switch (option) {
	case _SC_COBALT_VERSION:
		val = XENO_VERSION_CODE;
		break;
	case _SC_COBALT_NR_PIPES:
#if IS_ENABLED(CONFIG_XENO_OPT_PIPE)
		val = CONFIG_XENO_OPT_PIPE_NRDEV;
#endif
		break;
	case _SC_COBALT_NR_TIMERS:
		val = CONFIG_XENO_OPT_NRTIMERS;
		break;
	case _SC_COBALT_POLICIES:
		val = _SC_COBALT_SCHED_FIFO|_SC_COBALT_SCHED_RR;
		if (IS_ENABLED(CONFIG_XENO_OPT_SCHED_WEAK))
			val |= _SC_COBALT_SCHED_WEAK;
		if (IS_ENABLED(CONFIG_XENO_OPT_SCHED_SPORADIC))
			val |= _SC_COBALT_SCHED_SPORADIC;
		if (IS_ENABLED(CONFIG_XENO_OPT_SCHED_QUOTA))
			val |= _SC_COBALT_SCHED_QUOTA;
		if (IS_ENABLED(CONFIG_XENO_OPT_SCHED_TP))
			val |= _SC_COBALT_SCHED_TP;
		break;
	case _SC_COBALT_DEBUG:
		if (IS_ENABLED(CONFIG_XENO_OPT_DEBUG_COBALT))
			val |= _SC_COBALT_DEBUG_ASSERT;
		if (IS_ENABLED(CONFIG_XENO_OPT_DEBUG_CONTEXT))
			val |= _SC_COBALT_DEBUG_CONTEXT;
		if (IS_ENABLED(CONFIG_XENO_OPT_DEBUG_LOCKING))
			val |= _SC_COBALT_DEBUG_LOCKING;
		if (IS_ENABLED(CONFIG_XENO_OPT_DEBUG_SYNCH_RELAX))
			val |= _SC_COBALT_DEBUG_SYNCREL;
		if (IS_ENABLED(CONFIG_XENO_OPT_DEBUG_TRACE_RELAX))
			val |= _SC_COBALT_DEBUG_TRACEREL;
		break;
	case _SC_COBALT_WATCHDOG:
#if IS_ENABLED(CONFIG_XENO_OPT_WATCHDOG)
		val = CONFIG_XENO_OPT_WATCHDOG_TIMEOUT;
#endif
		break;
	default:
		return -EINVAL;
	}

	ret = __xn_safe_copy_from_user(u_buf, &val, sizeof(val));

	return ret ? -EFAULT : 0;
}

static int cobalt_ni(void)
{
	return -ENOSYS;
}

#define __syscast__(__handler)						\
	((int (*)(unsigned long, unsigned long,				\
		  unsigned long, unsigned long, unsigned long))(__handler))

#define __COBALT_CALL(__nr, __handler, __flags)	\
	[__nr] = { .handler = __syscast__(__handler), .flags = __xn_exec_##__flags }

#define __COBALT_NI	\
	{ .handler = __syscast__(cobalt_ni), .flags = 0 }

static struct cobalt_syscall cobalt_syscalls[] = {
	[0 ... __NR_COBALT_SYSCALLS-1] = __COBALT_NI,
	__COBALT_CALL(sc_cobalt_thread_create, cobalt_thread_create, init),
	__COBALT_CALL(sc_cobalt_thread_getpid, cobalt_thread_pid, current),
	__COBALT_CALL(sc_cobalt_thread_setschedparam_ex, cobalt_thread_setschedparam_ex, conforming),
	__COBALT_CALL(sc_cobalt_thread_getschedparam_ex, cobalt_thread_getschedparam_ex, current),
	__COBALT_CALL(sc_cobalt_sched_weightprio, cobalt_sched_weighted_prio, current),
	__COBALT_CALL(sc_cobalt_sched_yield, cobalt_sched_yield, primary),
	__COBALT_CALL(sc_cobalt_thread_setmode, cobalt_thread_setmode_np, primary),
	__COBALT_CALL(sc_cobalt_thread_setname, cobalt_thread_setname_np, current),
	__COBALT_CALL(sc_cobalt_thread_kill, cobalt_thread_kill, conforming),
	__COBALT_CALL(sc_cobalt_thread_getstat, cobalt_thread_stat, current),
	__COBALT_CALL(sc_cobalt_thread_join, cobalt_thread_join, primary),
	__COBALT_CALL(sc_cobalt_sem_init, cobalt_sem_init, current),
	__COBALT_CALL(sc_cobalt_sem_destroy, cobalt_sem_destroy, current),
	__COBALT_CALL(sc_cobalt_sem_post, cobalt_sem_post, current),
	__COBALT_CALL(sc_cobalt_sem_wait, cobalt_sem_wait, primary),
	__COBALT_CALL(sc_cobalt_sem_timedwait, cobalt_sem_timedwait, primary),
	__COBALT_CALL(sc_cobalt_sem_trywait, cobalt_sem_trywait, primary),
	__COBALT_CALL(sc_cobalt_sem_getvalue, cobalt_sem_getvalue, current),
	__COBALT_CALL(sc_cobalt_sem_open, cobalt_sem_open, current),
	__COBALT_CALL(sc_cobalt_sem_close, cobalt_sem_close, current),
	__COBALT_CALL(sc_cobalt_sem_unlink, cobalt_sem_unlink, current),
	__COBALT_CALL(sc_cobalt_sem_init_np, cobalt_sem_init_np, current),
	__COBALT_CALL(sc_cobalt_sem_broadcast_np, cobalt_sem_broadcast_np, current),
	__COBALT_CALL(sc_cobalt_sem_inquire, cobalt_sem_inquire, current),
	__COBALT_CALL(sc_cobalt_clock_getres, cobalt_clock_getres, current),
	__COBALT_CALL(sc_cobalt_clock_gettime, cobalt_clock_gettime, current),
	__COBALT_CALL(sc_cobalt_clock_settime, cobalt_clock_settime, current),
	__COBALT_CALL(sc_cobalt_clock_nanosleep, cobalt_clock_nanosleep, nonrestartable),
	__COBALT_CALL(sc_cobalt_mutex_init, cobalt_mutex_init, current),
	__COBALT_CALL(sc_cobalt_mutex_check_init, cobalt_mutex_check_init, current),
	__COBALT_CALL(sc_cobalt_mutex_destroy, cobalt_mutex_destroy, current),
	__COBALT_CALL(sc_cobalt_mutex_lock, cobalt_mutex_lock, primary),
	__COBALT_CALL(sc_cobalt_mutex_timedlock, cobalt_mutex_timedlock, primary),
	__COBALT_CALL(sc_cobalt_mutex_trylock, cobalt_mutex_trylock, primary),
	__COBALT_CALL(sc_cobalt_mutex_unlock, cobalt_mutex_unlock, nonrestartable),
	__COBALT_CALL(sc_cobalt_cond_init, cobalt_cond_init, current),
	__COBALT_CALL(sc_cobalt_cond_destroy, cobalt_cond_destroy, current),
	__COBALT_CALL(sc_cobalt_cond_wait_prologue, cobalt_cond_wait_prologue, nonrestartable),
	__COBALT_CALL(sc_cobalt_cond_wait_epilogue, cobalt_cond_wait_epilogue, primary),
	__COBALT_CALL(sc_cobalt_mq_open, cobalt_mq_open, lostage),
	__COBALT_CALL(sc_cobalt_mq_close, cobalt_mq_close, lostage),
	__COBALT_CALL(sc_cobalt_mq_unlink, cobalt_mq_unlink, lostage),
	__COBALT_CALL(sc_cobalt_mq_getattr, cobalt_mq_getattr, current),
	__COBALT_CALL(sc_cobalt_mq_setattr, cobalt_mq_setattr, current),
	__COBALT_CALL(sc_cobalt_mq_timedsend, cobalt_mq_timedsend, primary),
	__COBALT_CALL(sc_cobalt_mq_timedreceive, cobalt_mq_timedreceive, primary),
	__COBALT_CALL(sc_cobalt_mq_notify, cobalt_mq_notify, primary),
	__COBALT_CALL(sc_cobalt_sigwait, cobalt_sigwait, primary),
	__COBALT_CALL(sc_cobalt_sigwaitinfo, cobalt_sigwaitinfo, nonrestartable),
	__COBALT_CALL(sc_cobalt_sigtimedwait, cobalt_sigtimedwait, nonrestartable),
	__COBALT_CALL(sc_cobalt_sigpending, cobalt_sigpending, primary),
	__COBALT_CALL(sc_cobalt_kill, cobalt_kill, conforming),
	__COBALT_CALL(sc_cobalt_sigqueue, cobalt_sigqueue, conforming),
	__COBALT_CALL(sc_cobalt_timer_create, cobalt_timer_create, current),
	__COBALT_CALL(sc_cobalt_timer_delete, cobalt_timer_delete, current),
	__COBALT_CALL(sc_cobalt_timer_settime, cobalt_timer_settime, primary),
	__COBALT_CALL(sc_cobalt_timer_gettime, cobalt_timer_gettime, current),
	__COBALT_CALL(sc_cobalt_timer_getoverrun, cobalt_timer_getoverrun, current),
	__COBALT_CALL(sc_cobalt_timerfd_create, cobalt_timerfd_create, lostage),
	__COBALT_CALL(sc_cobalt_timerfd_gettime, cobalt_timerfd_gettime, current),
	__COBALT_CALL(sc_cobalt_timerfd_settime, cobalt_timerfd_settime, current),
	__COBALT_CALL(sc_cobalt_select, cobalt_select, nonrestartable),
	__COBALT_CALL(sc_cobalt_sched_minprio, cobalt_sched_min_prio, current),
	__COBALT_CALL(sc_cobalt_sched_maxprio, cobalt_sched_max_prio, current),
	__COBALT_CALL(sc_cobalt_monitor_init, cobalt_monitor_init, current),
	__COBALT_CALL(sc_cobalt_monitor_destroy, cobalt_monitor_destroy, primary),
	__COBALT_CALL(sc_cobalt_monitor_enter, cobalt_monitor_enter, primary),
	__COBALT_CALL(sc_cobalt_monitor_wait, cobalt_monitor_wait, nonrestartable),
	__COBALT_CALL(sc_cobalt_monitor_sync, cobalt_monitor_sync, nonrestartable),
	__COBALT_CALL(sc_cobalt_monitor_exit, cobalt_monitor_exit, primary),
	__COBALT_CALL(sc_cobalt_event_init, cobalt_event_init, current),
	__COBALT_CALL(sc_cobalt_event_destroy, cobalt_event_destroy, current),
	__COBALT_CALL(sc_cobalt_event_wait, cobalt_event_wait, primary),
	__COBALT_CALL(sc_cobalt_event_sync, cobalt_event_sync, current),
	__COBALT_CALL(sc_cobalt_event_inquire, cobalt_event_inquire, current),
	__COBALT_CALL(sc_cobalt_sched_setconfig_np, cobalt_sched_setconfig_np, current),
	__COBALT_CALL(sc_cobalt_sched_getconfig_np, cobalt_sched_getconfig_np, current),
	__COBALT_CALL(sc_cobalt_open, cobalt_open, lostage),
	__COBALT_CALL(sc_cobalt_socket, cobalt_socket, lostage),
	__COBALT_CALL(sc_cobalt_close, cobalt_close, lostage),
	__COBALT_CALL(sc_cobalt_mmap, cobalt_mmap, lostage),
	__COBALT_CALL(sc_cobalt_ioctl, cobalt_ioctl, probing),
	__COBALT_CALL(sc_cobalt_read, cobalt_read, probing),
	__COBALT_CALL(sc_cobalt_write, cobalt_write, probing),
	__COBALT_CALL(sc_cobalt_recvmsg, cobalt_recvmsg, probing),
	__COBALT_CALL(sc_cobalt_sendmsg, cobalt_sendmsg, probing),
	__COBALT_CALL(sc_cobalt_migrate, cobalt_migrate, current),
	__COBALT_CALL(sc_cobalt_arch, xnarch_local_syscall, current),
	__COBALT_CALL(sc_cobalt_bind, cobalt_bind, lostage),
	__COBALT_CALL(sc_cobalt_extend, cobalt_extend, lostage),
	__COBALT_CALL(sc_cobalt_info, cobalt_info, lostage),
	__COBALT_CALL(sc_cobalt_trace, cobalt_trace, current),
	__COBALT_CALL(sc_cobalt_heap_getstat, cobalt_heapstat, lostage),
	__COBALT_CALL(sc_cobalt_current, cobalt_get_current, current),
	__COBALT_CALL(sc_cobalt_mayday, cobalt_mayday, oneway),
	__COBALT_CALL(sc_cobalt_backtrace, cobalt_backtrace, current),
	__COBALT_CALL(sc_cobalt_serialdbg, cobalt_serialdbg, current),
	__COBALT_CALL(sc_cobalt_sysconf, cobalt_sysconf, current),
};
