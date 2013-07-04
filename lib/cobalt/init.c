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
#include <cobalt/kernel/uapi/heap.h>
#include <cobalt/uapi/rtdm/syscall.h>
#include <cobalt/ticks.h>
#include <xenomai/syscall.h>
#include <rtdk.h>
#include "sem_heap.h"
#include "internal.h"

int __cobalt_muxid = -1;

struct sigaction __cobalt_orig_sigdebug;

pthread_t __cobalt_main_tid;

int __rtdm_muxid = -1;

int __rtdm_fd_start = INT_MAX;

static int fork_handler_registered;

static void sigill_handler(int sig)
{
	const char m[] = "Xenomai disabled in kernel?\n";
	write(2, m, sizeof(m) - 1);
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

	__cobalt_main_tid = pthread_self();

	cobalt_ticks_init(sysinfo.clockfreq);

	return muxid;
}

/*
 * We give the Cobalt library constructor a high priority, so that
 * extension libraries may assume the core services are available when
 * their own constructor runs. Priorities 0-100 may be reserved by the
 * implementation on some platforms, and we may want to keep some
 * levels free for very high priority inits, so pick 200.
 */
static __attribute__ ((constructor(200)))
void __init_cobalt_interface(void)
{
	pthread_t tid = pthread_self();
	struct sched_param parm;
	int policy, muxid, ret;
	struct xnbindreq breq;
	struct sigaction sa;
	const char *p;

	muxid = bind_interface();
	if (muxid < 0) {
		report_error("interface unavailable");
		exit(EXIT_FAILURE);
	}

	sa.sa_sigaction = cobalt_handle_sigdebug;
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

	p = getenv("XENO_NOSHADOW");
	if (p && *p)
		goto no_shadow;

	/*
	 * Auto-shadow the current context if we can't be invoked from
	 * dlopen.
	 */
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		report_error("mlockall: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = __STD(pthread_getschedparam(tid, &policy, &parm));
	if (ret) {
		report_error("pthread_getschedparam: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}

	ret = __RT(pthread_setschedparam(tid, policy, &parm));
	if (ret) {
		report_error("pthread_setschedparam: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}

no_shadow:
	if (fork_handler_registered)
		return;

	ret = pthread_atfork(NULL, NULL, &__init_cobalt_interface);
	if (ret) {
		report_error("pthread_atfork: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}
	fork_handler_registered = 1;

	if (sizeof(struct __shadow_mutex) > sizeof(pthread_mutex_t)) {
		report_error("sizeof(pthread_mutex_t): %d <"
			     " sizeof(shadow_mutex): %d !",
			     (int) sizeof(pthread_mutex_t),
			     (int) sizeof(struct __shadow_mutex));
		exit(EXIT_FAILURE);
	}

	cobalt_print_init();
}
