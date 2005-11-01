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
#include <limits.h>
#include <xenomai/posix/posix.h>
#include <xenomai/posix/syscall.h>
#include <xenomai/rtdm/syscall.h>

int __pse51_muxid = -1;
int __rtdm_muxid  = -1;
int __rtdm_fd_start = INT_MAX;

static __attribute__((constructor)) void __init_posix_interface(void)

{
    int muxid;

    muxid = XENOMAI_SYSBIND(PSE51_SKIN_MAGIC,
			    XENOMAI_FEAT_DEP,
			    XENOMAI_ABI_REV);
    if (muxid < 0)
	{
	fprintf(stderr,"Xenomai: POSIX skin or user-space support unavailable.\n");
	fprintf(stderr,"(did you load the xeno_posix.ko module?)\n");
	exit(1);
	}

    __pse51_muxid = muxid;

    muxid = XENOMAI_SYSBIND(RTDM_SKIN_MAGIC,
			    XENOMAI_FEAT_DEP,
			    XENOMAI_ABI_REV);
    if (muxid > 0)
        {
        __rtdm_muxid    = muxid;
        __rtdm_fd_start = FD_SETSIZE - XENOMAI_SKINCALL0(__rtdm_muxid,
                                                         __rtdm_fdcount);
        }
}
