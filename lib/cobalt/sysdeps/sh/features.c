/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <cobalt/wrappers.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

struct xnarch_tsc_area;

__attribute__((weak))
volatile struct xnarch_tsc_area *xeno_sh_tsc = NULL;

__attribute__((weak))
volatile unsigned long *xeno_sh_tcnt = NULL;

static volatile void *__xeno_kmem_map(unsigned long pa, unsigned int pagesz)
{
	void *p;
	int fd;

	fd = __STD(open("/dev/mem", O_RDWR | O_SYNC));
	if (fd == -1) {
		report_error("open(/dev/mem): %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	p = mmap(NULL, pagesz, PROT_READ | PROT_WRITE, MAP_SHARED,
		 fd, pa & ~(pagesz - 1));
	if (p == MAP_FAILED) {
		report_error("mmap(/dev/mem): %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	__STD(close(fd));

	return (volatile void *)(p + (pa & (pagesz - 1)));
}

void cobalt_chech_features(struct xnfeatinfo *finfo)
{
	unsigned int pagesz = sysconf(_SC_PAGESIZE);

	xeno_sh_tsc = __xeno_kmem_map(finfo->feat_arch.hrclock_membase, pagesz);
	xeno_sh_tcnt = __xeno_kmem_map(xeno_sh_tsc->counter_pa, pagesz);
}
