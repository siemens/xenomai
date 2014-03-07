/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <semaphore.h>
#include <boilerplate/ancillaries.h>
#include <cobalt/uapi/kernel/heap.h>
#include <cobalt/uapi/rtdm/syscall.h>
#include <cobalt/ticks.h>
#include <asm/xenomai/syscall.h>
#include "sem_heap.h"
#include "internal.h"
#include "init.h"

__attribute__ ((weak))
int __cobalt_defer_init = 0;

__attribute__ ((weak))
int __cobalt_main_prio = -1;

int __cobalt_muxid = -1;

struct sigaction __cobalt_orig_sigdebug;

pthread_t __cobalt_main_tid;

int __rtdm_muxid = -1;

int __rtdm_fd_start = INT_MAX;

static void sigill_handler(int sig)
{
	const char m[] = "no Xenomai support in kernel?\n";
	ssize_t rc __attribute__ ((unused));
	rc = write(2, m, sizeof(m) - 1);
	exit(EXIT_FAILURE);
}

static int bind_interface(void)
{
	sighandler_t old_sigill_handler;
	struct xnsysinfo sysinfo;
	struct xnbindreq breq;
	struct xnfeatinfo *f;
	int ret, muxid;

	/* Some sanity checks first. */
	if (access(XNHEAP_DEV_NAME, 0)) {
		report_error("%s is missing\n(chardev, major=10 minor=%d)",
			     XNHEAP_DEV_NAME, XNHEAP_DEV_MINOR);
		exit(EXIT_FAILURE);
	}

	old_sigill_handler = signal(SIGILL, sigill_handler);
	if (old_sigill_handler == SIG_ERR) {
		report_error("signal(SIGILL): %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	f = &breq.feat_ret;
	breq.feat_req = XENOMAI_FEAT_DEP;
	breq.abi_rev = XENOMAI_ABI_REV;
	muxid = XENOMAI_SYSBIND(COBALT_BINDING_MAGIC, &breq);

	signal(SIGILL, old_sigill_handler);

	switch (muxid) {
	case -EINVAL:
		report_error("incompatible feature set");
		report_error_cont("(userland requires \"%s\", kernel provides \"%s\", missing=\"%s\")",
				  f->feat_man_s, f->feat_all_s, f->feat_mis_s);
		exit(EXIT_FAILURE);

	case -ENOEXEC:
		report_error("incompatible ABI revision level");
		report_error_cont("(user-space requires '%lu', kernel provides '%lu')",
			XENOMAI_ABI_REV, f->feat_abirev);
		exit(EXIT_FAILURE);

	case -ENOSYS:
	case -ESRCH:
		return -1;
	}

	if (muxid < 0) {
		report_error("binding failed: %s", strerror(-muxid));
		exit(EXIT_FAILURE);
	}

	cobalt_check_features(f);

	ret = XENOMAI_SYSCALL2(sc_nucleus_info, muxid, &sysinfo);
	if (ret) {
		report_error("sysinfo failed: %s", strerror(-ret));
		exit(EXIT_FAILURE);
	}

	cobalt_init_sem_heaps();

	cobalt_init_current_keys();

	cobalt_ticks_init(sysinfo.clockfreq);

	return muxid;
}

static void __init_cobalt(void);

void __libcobalt_init(void)
{
	struct xnbindreq breq;
	struct sigaction sa;
	int muxid, ret;

	muxid = bind_interface();
	if (muxid < 0) {
		report_error("interface unavailable");
		exit(EXIT_FAILURE);
	}

	sa.sa_sigaction = cobalt_sigdebug_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGXCPU, &sa, &__cobalt_orig_sigdebug);

	__cobalt_muxid = __xn_mux_shifted_id(muxid);

	breq.feat_req = XENOMAI_FEAT_DEP;
	breq.abi_rev = XENOMAI_ABI_REV;
	muxid = XENOMAI_SYSBIND(RTDM_BINDING_MAGIC, &breq);
	if (muxid > 0) {
		__rtdm_muxid = __xn_mux_shifted_id(muxid);
		__rtdm_fd_start = FD_SETSIZE - XENOMAI_SKINCALL0(__rtdm_muxid,
								 sc_rtdm_fdcount);
	}

	/*
	 * Upon fork, in case the parent required init deferral, this
	 * is the forkee's responsibility to call __libcobalt_init()
	 * for bootstrapping the services the same way.
	 */
	ret = pthread_atfork(NULL, NULL, __init_cobalt);
	if (ret) {
		report_error("pthread_atfork: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}

	if (sizeof(struct cobalt_mutex_shadow) > sizeof(pthread_mutex_t)) {
		report_error("sizeof(pthread_mutex_t): %d <"
			     " sizeof(cobalt_mutex_shadow): %d!",
			     (int) sizeof(pthread_mutex_t),
			     (int) sizeof(struct cobalt_mutex_shadow));
		exit(EXIT_FAILURE);
	}
	if (sizeof(struct cobalt_cond_shadow) > sizeof(pthread_cond_t)) {
		report_error("sizeof(pthread_cond_t): %d <"
			     " sizeof(cobalt_cond_shadow): %d!",
			     (int) sizeof(pthread_cond_t),
			     (int) sizeof(struct cobalt_cond_shadow));
		exit(EXIT_FAILURE);
	}
	if (sizeof(struct cobalt_sem_shadow) > sizeof(sem_t)) {
		report_error("sizeof(sem_t): %d <"
			     " sizeof(cobalt_sem_shadow): %d!",
			     (int) sizeof(sem_t),
			     (int) sizeof(struct cobalt_sem_shadow));
		exit(EXIT_FAILURE);
	}

	cobalt_thread_init();
	cobalt_print_init();
	boilerplate_init();
}

static __libcobalt_ctor void __init_cobalt(void)
{
	pthread_t tid = pthread_self();
	struct sched_param parm;
	int policy, ret;
	const char *p;

	__cobalt_main_tid = tid;

	if (__cobalt_defer_init)
		return;

	__libcobalt_init();

	p = getenv("XENO_NOSHADOW");
	if (p && *p)
		return;

	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		report_error("mlockall: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = __STD(pthread_getschedparam(tid, &policy, &parm));
	if (ret) {
		report_error("pthread_getschedparam: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}

	/*
	 * Switch the main thread to a Xenomai shadow.
	 * __cobalt_main_prio might have been overriden by
	 * some compilation unit which has been linked in, to force
	 * the scheduling parameters. Otherwise, the current policy
	 * and priority are reused, for declaring the thread to the
	 * Xenomai scheduler.
	 *
	 * SCHED_FIFO is assumed for __cobalt_main_prio > 0.
	 */
	if (__cobalt_main_prio > 0) {
		policy = SCHED_FIFO;
		parm.sched_priority = __cobalt_main_prio;
	} else if (__cobalt_main_prio == 0) {
		policy = SCHED_OTHER;
		parm.sched_priority = 0;
	}
	
	ret = __RT(pthread_setschedparam(tid, policy, &parm));
	if (ret) {
		report_error("pthread_setschedparam: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}
}
