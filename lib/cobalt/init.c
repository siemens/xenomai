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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <cobalt/posix.h>
#include <cobalt/syscall.h>
#include <rtdm/syscall.h>
#include <kernel/cobalt/mutex.h>
#include <rtdk.h>
#include <asm/xenomai/bits/bind.h>

int __cobalt_muxid = -1;
int __rtdm_muxid = -1;
int __rtdm_fd_start = INT_MAX;
static int fork_handler_registered;

int __wrap_pthread_setschedparam(pthread_t, int, const struct sched_param *);
void cobalt_clock_init(int);

static __attribute__ ((constructor))
void __init_cobalt_interface(void)
{
	struct sched_param parm;
	struct xnbindreq breq;
	int policy, muxid, ret;
	const char *p;

	rt_print_auto_init(1);

	muxid =
	    xeno_bind_skin(COBALT_SKIN_MAGIC, "POSIX", "xeno_posix");

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
	ret = __STD(pthread_getschedparam(pthread_self(), &policy, &parm));
	if (ret) {
		fprintf(stderr, "Xenomai Posix skin init: "
			"pthread_getschedparam: %s\n", strerror(ret));
		exit(EXIT_FAILURE);
	}

	ret = __wrap_pthread_setschedparam(pthread_self(), policy, &parm);
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
