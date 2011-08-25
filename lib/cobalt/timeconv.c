/*
 * Copyright (C) 2010 Jan Kiszka <jan.kiszka@siemens.com>.
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include <asm/xenomai/syscall.h>
#include <asm-generic/xenomai/bits/timeconv.h>

static unsigned long long hrclock_freq;

static void xeno_init_timeconv_inner(void)
{
	xnarch_init_timeconv(hrclock_freq);
}

void xeno_init_timeconv(int muxid)
{
	static pthread_once_t init_timeconv_once = PTHREAD_ONCE_INIT;
	xnsysinfo_t sysinfo;
	int ret;

	ret = XENOMAI_SYSCALL2(__xn_sys_info, muxid, &sysinfo);
	if (ret) {
		fprintf(stderr, "Xenomai: sys_info failed: %s\n",
			strerror(-ret));
		exit(EXIT_FAILURE);
	}
	hrclock_freq = sysinfo.clockfreq;
	pthread_once(&init_timeconv_once, &xeno_init_timeconv_inner);
}
