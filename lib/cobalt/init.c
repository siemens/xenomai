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
#include <cobalt/ticks.h>
#include <asm/xenomai/syscall.h>
#include "umm.h"
#include "internal.h"
#include "init.h"

/**
 * @ingroup cobalt
 * @defgroup cobalt_api POSIX interface
 *
 * The Cobalt/POSIX interface is an implementation of a subset of the
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/">
 * Single Unix specification</a> over the Cobalt core.
 */

__weak int __cobalt_defer_init = 0;

__weak int __cobalt_no_shadow = 0;

__weak int __cobalt_control_bind = 0;

__weak int __cobalt_main_prio = -1;

struct sigaction __cobalt_orig_sigdebug;

pthread_t __cobalt_main_ptid;

static void sigill_handler(int sig)
{
	const char m[] = "no Xenomai support in kernel?\n";
	ssize_t rc __attribute__ ((unused));
	rc = write(2, m, sizeof(m) - 1);
	exit(EXIT_FAILURE);
}

static void low_init(void)
{
	sighandler_t old_sigill_handler;
	struct cobalt_bindreq breq;
	struct cobalt_featinfo *f;
	int ret;

	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		report_error("mlockall: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	old_sigill_handler = signal(SIGILL, sigill_handler);
	if (old_sigill_handler == SIG_ERR) {
		report_error("signal(SIGILL): %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	f = &breq.feat_ret;
	breq.feat_req = XENOMAI_FEAT_DEP;
	if (__cobalt_control_bind)
		breq.feat_req |= __xn_feat_control;
	breq.abi_rev = XENOMAI_ABI_REV;
	ret = XENOMAI_SYSBIND(&breq);

	signal(SIGILL, old_sigill_handler);

	switch (ret) {
	case 0:
		break;
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

	default:
		report_error("binding failed: %s", strerror(-ret));
		exit(EXIT_FAILURE);
	}

	cobalt_check_features(f);
	cobalt_init_umm(f->vdso_offset);
	cobalt_init_current_keys();
	cobalt_ticks_init(f->clock_freq);
}

static void __init_cobalt(void);

static void cobalt_fork_handler(void)
{
	cobalt_unmap_umm();
	cobalt_clear_tsd();
	cobalt_print_init_atfork();
#ifdef HAVE_PTHREAD_ATFORK
	/*
	 * Upon fork, in case the parent required init deferral, this
	 * is the forkee's responsibility to call __libcobalt_init()
	 * for bootstrapping the services the same way. On systems
	 * with no fork() support, clients are not supposed to, well,
	 * fork in the first place, so we don't take any provision for
	 * this event.
	 */
	__init_cobalt();
#endif
}

void __libcobalt_init(void)
{
	struct sigaction sa;

	low_init();

	sa.sa_sigaction = cobalt_sigdebug_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, &__cobalt_orig_sigdebug);

	/*
	 * NOTE: a placeholder for pthread_atfork() may return an
	 * error status with uClibc, so we don't check the return
	 * value on purpose.
	 */
	pthread_atfork(NULL, NULL, cobalt_fork_handler);

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

static inline void dump_configuration(void)
{
	int n;

	for (n = 0; config_strings[n]; n++)
		puts(config_strings[n]);
}

static __libcobalt_ctor void __init_cobalt(void)
{
	pthread_t ptid = pthread_self();
	struct sched_param parm;
	int policy, ret;
	const char *p;

	p = getenv("XENO_CONFIG_OUTPUT");
	if (p && *p) {
		dump_configuration();
		_exit(0);
	}

#ifndef CONFIG_SMP
	ret = get_static_cpu_count();
	if (ret > 0)
		report_error("running non-SMP libraries on SMP kernel?");
#endif

	__cobalt_main_ptid = ptid;
	cobalt_default_mutexattr_init();
	cobalt_default_condattr_init();

	if (__cobalt_defer_init)
		return;

	__libcobalt_init();

	if (__cobalt_no_shadow)
		return;

	p = getenv("XENO_NOSHADOW");
	if (p && *p)
		return;

	ret = __STD(pthread_getschedparam(ptid, &policy, &parm));
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

	ret = __RT(pthread_setschedparam(ptid, policy, &parm));
	if (ret) {
		report_error("pthread_setschedparam: %s", strerror(ret));
		exit(EXIT_FAILURE);
	}
}

static void __attribute__((destructor)) __fini_cobalt(void)
{
	cobalt_print_exit();
}
