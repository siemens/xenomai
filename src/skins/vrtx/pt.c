/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <nucleus/heap.h>
#include <vrtx/vrtx.h>

extern int __vrtx_muxid;

void *xeno_map_heap(struct xnheap_desc *hd);

static int __map_pt_memory(const vrtx_pdesc_t *pdesc)
{
	struct xnheap_desc hd;
	caddr_t mapbase;

	hd.handle = (unsigned long)pdesc->ptcb;
	hd.size = pdesc->ptsize;
	hd.area = pdesc->area;
	mapbase = xeno_map_heap(&hd);
	if (mapbase == MAP_FAILED)
		return -errno;

	return XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_pbind,
				 pdesc->pid, mapbase);
}

int sc_pcreate(int pid, char *paddr,
	       long psize, long bsize, int *errp)
{
	vrtx_pdesc_t pdesc;

	if (paddr)
		fprintf(stderr,
			"sc_pcreate() - paddr parameter ignored from user-space context\n");

	*errp = XENOMAI_SKINCALL3(__vrtx_muxid,
				  __vrtx_pcreate, psize, bsize, &pdesc);
	if (*errp)
		return 0;

	pid = pdesc.pid;
	*errp = __map_pt_memory(&pdesc);

	if (*errp)
		/* If the mapping fails, make sure we don't leave a dandling
		   partition in kernel space -- remove it. */
	    XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_pdelete, pid, 1);

	return pid;
}

void sc_pdelete(int pid, int opt, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_pdelete, pid, opt);
}

char *sc_gblock(int pid, int *errp)
{
	char *buf = NULL;
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_gblock, pid, &buf);
	return buf;
}

void sc_rblock(int pid, char *buf, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_rblock, pid, buf);
}

void sc_pinquiry(u_long info[3], int pid, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_pinquiry, info, pid);
}
