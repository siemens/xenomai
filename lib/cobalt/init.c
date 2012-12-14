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
#include <nucleus/heap.h>
#include <rtdm/syscall.h>
#include <cobalt/syscall.h>
#include <kernel/cobalt/mutex.h>
#include <rtdk.h>
#include <asm/xenomai/bits/bind.h>
#include <asm-generic/xenomai/timeconv.h>
#include <asm-generic/xenomai/stack.h>
#include <asm-generic/xenomai/sem_heap.h>
#include "internal.h"

int __cobalt_muxid = -1;
int __rtdm_muxid = -1;
int __rtdm_fd_start = INT_MAX;
static int fork_handler_registered;
static pthread_t xeno_main_tid;
struct xnfeatinfo xeno_featinfo;

int __RT(pthread_setschedparam)(pthread_t, int, const struct sched_param *);
void cobalt_clock_init(int);

static void sigill_handler(int sig)
{
	fprintf(stderr, "Xenomai disabled in kernel?\n");
	exit(EXIT_FAILURE);
}

#ifdef xeno_arch_features_check
static void do_init_arch_features(void)
{
	xeno_arch_features_check(&xeno_featinfo);
}
static void init_arch_features(void)
{
	static pthread_once_t init_archfeat_once = PTHREAD_ONCE_INIT;
	pthread_once(&init_archfeat_once, do_init_arch_features);
}
#else  /* !xeno_init_arch_features */
#define init_arch_features()	do { } while (0)
#endif /* !xeno_arch_features_check */

void xeno_fault_stack(void)
{
	if (pthread_self() == xeno_main_tid) {
		char stk[xeno_stacksize(1)];
		stk[0] = stk[sizeof(stk) - 1] = 0xA5;
	}
}

static int bind_interface(void)
{
	sighandler_t old_sigill_handler;
	struct xnbindreq breq;
	struct xnfeatinfo *f;
	int muxid;

	/* Some sanity checks first. */
	if (access(XNHEAP_DEV_NAME, 0)) {
		fprintf(stderr, "Xenomai: %s is missing\n(chardev, major=10 minor=%d)\n",
			XNHEAP_DEV_NAME, XNHEAP_DEV_MINOR);
		exit(EXIT_FAILURE);
	}

	old_sigill_handler = signal(SIGILL, sigill_handler);
	if (old_sigill_handler == SIG_ERR) {
		perror("signal(SIGILL)");
		exit(EXIT_FAILURE);
	}

	f = &breq.feat_ret;
	breq.feat_req = XENOMAI_FEAT_DEP;
	breq.abi_rev = XENOMAI_ABI_REV;
	muxid = XENOMAI_SYSBIND(COBALT_SKIN_MAGIC, &breq);

	signal(SIGILL, old_sigill_handler);

	switch (muxid) {
	case -EINVAL:
		fprintf(stderr, "Xenomai: incompatible feature set\n");
		fprintf(stderr,
			"(userland requires \"%s\", kernel provides \"%s\", missing=\"%s\").\n",
			f->feat_man_s, f->feat_all_s, f->feat_mis_s);
		exit(EXIT_FAILURE);

	case -ENOEXEC:
		fprintf(stderr, "Xenomai: incompatible ABI revision level\n");
		fprintf(stderr, "(user-space requires '%lu', kernel provides '%lu').\n",
			XENOMAI_ABI_REV, f->feat_abirev);
		exit(EXIT_FAILURE);

	case -ENOSYS:
	case -ESRCH:
		return -1;
	}

	if (muxid < 0) {
		fprintf(stderr, "Xenomai: binding failed: %s.\n",
			strerror(-muxid));
		exit(EXIT_FAILURE);
	}

	xeno_featinfo = *f;
	init_arch_features();

	xeno_init_sem_heaps();

	xeno_init_current_keys();

	xeno_main_tid = pthread_self();

	xeno_init_timeconv(muxid);

	return muxid;
}

static __attribute__ ((constructor))
void __init_cobalt_interface(void)
{
	pthread_t tid = pthread_self();
	struct sched_param parm;
	struct xnbindreq breq;
	int policy, muxid, ret;
	struct sigaction sa;
	const char *p;

	rt_print_auto_init(1);

	muxid = bind_interface();
	if (muxid < 0) {
		fprintf(stderr,
			"Xenomai: Cobalt interface unavailable\n");
		exit(EXIT_FAILURE);
	}

	sa.sa_sigaction = cobalt_handle_sigdebug;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGXCPU, &sa, NULL);

	cobalt_clock_init(muxid);

	__cobalt_muxid = __xn_mux_shifted_id(muxid);

	breq.feat_req = XENOMAI_FEAT_DEP;
	breq.abi_rev = XENOMAI_ABI_REV;
	muxid = XENOMAI_SYSBIND(RTDM_SKIN_MAGIC, &breq);
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
		perror("Xenomai Posix skin init: mlockall");
		exit(EXIT_FAILURE);
	}

	ret = __STD(pthread_getschedparam(tid, &policy, &parm));
	if (ret) {
		fprintf(stderr, "Xenomai Posix skin init: "
			"pthread_getschedparam: %s\n", strerror(ret));
		exit(EXIT_FAILURE);
	}

	ret = __RT(pthread_setschedparam(tid, policy, &parm));
	if (ret) {
		fprintf(stderr, "Xenomai Posix skin init: "
			"pthread_setschedparam: %s\n", strerror(ret));
		exit(EXIT_FAILURE);
	}

no_shadow:
	if (fork_handler_registered)
		return;

	ret = pthread_atfork(NULL, NULL, &__init_cobalt_interface);
	if (ret) {
		fprintf(stderr, "Xenomai Posix skin init: "
			"pthread_atfork: %s\n", strerror(ret));
		exit(EXIT_FAILURE);
	}
	fork_handler_registered = 1;

	if (sizeof(struct __shadow_mutex) > sizeof(pthread_mutex_t)) {
		fprintf(stderr, "sizeof(pthread_mutex_t): %d <"
			" sizeof(shadow_mutex): %d !\n",
			(int) sizeof(pthread_mutex_t),
			(int) sizeof(struct __shadow_mutex));
		exit(EXIT_FAILURE);
	}
}
