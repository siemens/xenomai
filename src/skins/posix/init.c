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
#include <posix/posix.h>
#include <posix/syscall.h>
#include <rtdm/syscall.h>
#include <asm-generic/sigshadow.h>
#include <posix/mutex.h>
#include <rtdk.h>

#include <asm-generic/xenomai/bind.h>
#include <asm-generic/xenomai/current.h>

int __pse51_muxid = -1;
int __pse51_rtdm_muxid = -1;
int __pse51_rtdm_fd_start = INT_MAX;
static int fork_handler_registered;

int __wrap_pthread_setschedparam(pthread_t, int, const struct sched_param *);
void pse51_clock_init(int);

static __constructor__ void __init_posix_interface(void)
{
	struct sched_param parm;
	int policy;
	int muxid, err;
	const char *noshadow;

	rt_print_auto_init(1);

	muxid =
	    xeno_bind_skin(PSE51_SKIN_MAGIC, "POSIX", "xeno_posix");

#ifdef XNARCH_HAVE_NONPRIV_TSC
	pse51_clock_init(muxid);
#endif /* XNARCH_HAVE_NONPRIV_TSC */

	__pse51_muxid = __xn_mux_shifted_id(muxid);

	muxid = XENOMAI_SYSBIND(RTDM_SKIN_MAGIC,
				XENOMAI_FEAT_DEP, XENOMAI_ABI_REV, NULL);
	if (muxid > 0) {
		__pse51_rtdm_muxid = __xn_mux_shifted_id(muxid);
		__pse51_rtdm_fd_start = FD_SETSIZE - XENOMAI_SKINCALL0(__pse51_rtdm_muxid,
								 __rtdm_fdcount);
	}

	noshadow = getenv("XENO_NOSHADOW");
	if ((!noshadow || !*noshadow) && xeno_get_current() == XN_NO_HANDLE) {
		err = __real_pthread_getschedparam(pthread_self(), &policy,
						   &parm);
		if (err) {
			fprintf(stderr, "Xenomai Posix skin init: "
				"pthread_getschedparam: %s\n", strerror(err));
			exit(EXIT_FAILURE);
		}

		err = __wrap_pthread_setschedparam(pthread_self(), policy,
						   &parm);
		if (err) {
			fprintf(stderr, "Xenomai Posix skin init: "
				"pthread_setschedparam: %s\n", strerror(err));
			exit(EXIT_FAILURE);
		}
	}

	if (fork_handler_registered)
		return;

	err = pthread_atfork(NULL, NULL, &__init_posix_interface);
	if (err) {
		fprintf(stderr, "Xenomai Posix skin init: "
			"pthread_atfork: %s\n", strerror(err));
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
