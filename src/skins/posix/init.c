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
int __pse51_sigxcpu_no_mlock = 1;
int __rtdm_muxid  = -1;
int __rtdm_fd_start = INT_MAX;

void __handle_lock_alert (int sig)

{
    struct sigaction sa;

    if (__pse51_sigxcpu_no_mlock)
	{
	fprintf(stderr,"Xenomai: process memory not locked (missing mlockall?)\n");
	fflush(stderr);
	exit(4);
	}
    else
	{
	/* PTHREAD_WARNSW was set for the thread but no user-defined
	   handler has been set to override our internal handler, so
	   let's invoke the default signal action. */
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGXCPU,&sa,NULL);
	pthread_kill(pthread_self(),SIGXCPU);
	}
}

static __attribute__((constructor)) void __init_posix_interface(void)

{
    struct sigaction sa;
    xnfeatinfo_t finfo;
    int muxid;

    muxid = XENOMAI_SYSBIND(PSE51_SKIN_MAGIC,
			    XENOMAI_FEAT_DEP,
			    XENOMAI_ABI_REV,
			    &finfo);
    switch (muxid)
	{
	case -EINVAL:

	    fprintf(stderr,"Xenomai: incompatible feature set\n");
	    fprintf(stderr,"(required=\"%s\", present=\"%s\", missing=\"%s\").\n",
		    finfo.feat_man_s,finfo.feat_all_s,finfo.feat_mis_s);
	    exit(1);

	case -ENOEXEC:

	    fprintf(stderr,"Xenomai: incompatible ABI revision level\n");
	    fprintf(stderr,"(needed=%lu, current=%lu).\n",
		    XENOMAI_ABI_REV,finfo.abirev);
	    exit(1);

	case -ENOSYS:
	case -ESRCH:

	    fprintf(stderr,"Xenomai: POSIX skin or CONFIG_XENO_OPT_PERVASIVE disabled.\n");
	    fprintf(stderr,"(modprobe xeno_posix?)\n");
	    exit(1);

	default:

	    if (muxid < 0)
		{
		fprintf(stderr,"Xenomai: binding failed: %s.\n",strerror(-muxid));
		exit(1);
		}

	    __pse51_muxid = muxid;
	    break;
	}

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

    /* Install a SIGXCPU handler to intercept alerts about unlocked
       process memory. */

    sa.sa_handler = &__handle_lock_alert;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGXCPU,&sa,NULL);
}
