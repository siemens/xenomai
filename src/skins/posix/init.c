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

int __pse51_muxid = -1;
int __rtdm_muxid  = -1;
int __rtdm_fd_start = INT_MAX;

int __wrap_pthread_setschedparam(pthread_t, int, const struct sched_param *);

static __attribute__((constructor)) void __init_posix_interface(void)

{
    sighandler_t oldhandler;
    struct sched_param parm;
    int muxid, err;
    
    __pse51_muxid = xeno_user_skin_init(PSE51_SKIN_MAGIC, "POSIX", "xeno_posix");

    muxid = XENOMAI_SYSBIND(RTDM_SKIN_MAGIC,
			    XENOMAI_FEAT_DEP,
			    XENOMAI_ABI_REV,
			    NULL);
    if (muxid > 0)
        {
        __rtdm_muxid    = muxid;
        __rtdm_fd_start = FD_SETSIZE - XENOMAI_SKINCALL0(__rtdm_muxid,
                                                         __rtdm_fdcount);
        }

    /* Shadow the main thread. Ignoring SIGXCPU for now, but in order to do
       anything useful the application will have to call other services. */
    oldhandler = signal(SIGXCPU, SIG_IGN);

    if (oldhandler == SIG_ERR) 
        {
        perror("signal");
        exit(EXIT_FAILURE);
        }

    parm.sched_priority = 0;
    if ((err = __wrap_pthread_setschedparam(pthread_self(),SCHED_OTHER,&parm)))
        {
        fprintf(stderr, "pthread_setschedparam: %s\n", strerror(err));
        exit(EXIT_FAILURE);
        }

    if (signal(SIGXCPU, oldhandler) == SIG_ERR)
        {
        perror("signal");
        exit(EXIT_FAILURE);
        }
}
