/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <uvm/syscall.h>
#include <uvm/uvm.h>

extern int __uvm_muxid;

extern xnsysinfo_t __uvm_info;

static unsigned long long ullimd(unsigned long long ull, u_long m, u_long d)

{
    u_long h, l, mlh, mll, qh, r, ql;
    unsigned long long mh, ml;

    h = ull >> 32; l = ull & 0xffffffff; /* Split ull. */
    mh = (unsigned long long) h * m;
    ml = (unsigned long long) l * m;
    mlh = ml >> 32; mll = ml & 0xffffffff; /* Split ml. */
    mh += mlh;
    qh = mh / d;
    r = mh % d;
    ml = (((unsigned long long) r) << 32) + mll; /* assemble r and mll */
    ql = ml / d;

    return (((unsigned long long) qh) << 32) + ql;
}

static long long llimd(long long ll, u_long m, u_long d) {

    if(ll < 0)
        return -ullimd(-ll, m, d);
    return ullimd(ll, m, d);
}

int uvm_system_info (xnsysinfo_t *infop)

{
    memcpy(infop,&__uvm_info,sizeof(*infop));
    return 0;
}

static void uvm_sigharden (int sig)
{
    XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_XENO_DOMAIN);
}

int uvm_thread_shadow (const char *name,
			  void *uhandle,
			  void **khandlep)
{
    signal(SIGCHLD,&uvm_sigharden);
    /* Move the current Linux task into the Xenomai realm. */
    return XENOMAI_SKINCALL3(__uvm_muxid,
			     __uvm_thread_shadow,
			     name,
			     khandlep,
			     uhandle);
}

int uvm_thread_create (const char *name,
		       void *uhandle,
		       xncompletion_t *completionp,
		       void **khandlep)
{
    XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_LINUX_DOMAIN);

    /* Move the current Linux task into the Xenomai realm, but do not
       start the mated shadow thread. Caller will need to wait on the
       barrier (uvm_thread_barrier()) for the start event
       (uvm_thread_start()). */
    return XENOMAI_SKINCALL4(__uvm_muxid,
			     __uvm_thread_create,
			     name,
			     khandlep,
			     uhandle,
			     completionp);
}

int uvm_thread_barrier (void)

{
    void (*entry)(void *), *cookie;
    int err;

    signal(SIGCHLD,&uvm_sigharden);

    /* Make the current Linux task wait on the barrier for its mated
       shadow thread to be started. The barrier could be released in
       order to process Linux signals while the Xenomai shadow is still
       dormant; in such a case, resume wait. */

    do
	err = XENOMAI_SYSCALL2(__xn_sys_barrier,
			       &entry,
			       &cookie);
    while (err == -EINTR);

    return err;
}

int uvm_thread_start (void *khandle)

{
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_thread_start,
			     khandle);
}

int uvm_thread_sync (xncompletion_t *completionp)

{
    return XENOMAI_SYSCALL1(__xn_sys_completion,
			    completionp);
}

int uvm_thread_wait_period (void)

{
    return XENOMAI_SKINCALL0(__uvm_muxid,
			     __uvm_thread_wait_period);
}

int uvm_thread_idle (unsigned long *lockp)
{
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_thread_idle,
			     lockp);
}

int uvm_thread_cancel (void *deadhandle, void *nexthandle)
{
    return XENOMAI_SKINCALL2(__uvm_muxid,
			     __uvm_thread_cancel,
			     deadhandle,
			     nexthandle);
}

int uvm_thread_activate (void *nexthandle, void *prevhandle)
{
    return XENOMAI_SKINCALL2(__uvm_muxid,
			     __uvm_thread_activate,
			     nexthandle,
			     prevhandle);
}

int uvm_thread_hold (unsigned long *pendp)
{
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_thread_hold,
			     pendp);
}

int uvm_thread_release (unsigned long *lockp)
{
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_thread_release,
			     lockp);
}

int uvm_thread_set_periodic (nanotime_t idate,
				   nanotime_t period)
{
    return XENOMAI_SKINCALL2(__uvm_muxid,
			     __uvm_thread_set_periodic,
			     &idate,
			     &period);
}

int uvm_timer_start (nanotime_t nstick)

{
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_timer_start,
			     &nstick);
}

int uvm_timer_stop (void)
{

    return XENOMAI_SKINCALL0(__uvm_muxid,
			     __uvm_timer_stop);
}

int uvm_timer_read (nanotime_t *tp)

{
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_timer_read,
			     tp);
}

int uvm_timer_tsc (nanotime_t *tp)

{
#ifdef CONFIG_XENO_HW_DIRECT_TSC
    *tp = __xn_rdtsc();
    return 0;
#else /* !CONFIG_XENO_HW_DIRECT_TSC */
    return XENOMAI_SKINCALL1(__uvm_muxid,
			     __uvm_timer_tsc,
			     tp);
#endif /* CONFIG_XENO_HW_DIRECT_TSC */
}

int uvm_timer_ns2tsc (nanostime_t ns, nanostime_t *ptsc)

{
    if (__uvm_muxid == 0)
	return -ENOSYS;

    *ptsc = llimd(ns, __uvm_info.cpufreq, 1000000000);

    return 0;
}

int uvm_timer_tsc2ns (nanostime_t tsc, nanostime_t *pns)

{
    if (__uvm_muxid == 0)
	return -ENOSYS;

    *pns = llimd(tsc, 1000000000, __uvm_info.cpufreq);

    return 0;
}
