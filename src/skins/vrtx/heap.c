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

static int __map_heap_memory(const vrtx_hdesc_t *hdesc)
{
	struct xnheap_desc hd;
	caddr_t mapbase;

	hd.handle = (unsigned long)hdesc->hcb;
	hd.size = hdesc->hsize;
	hd.area = hdesc->area;
	mapbase = xeno_map_heap(&hd);
	if (mapbase == MAP_FAILED)
		return -errno;

	return XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_hbind,
				 hdesc->hid, mapbase);
}

int sc_hcreate(char *heapaddr,
	       unsigned long heapsize, unsigned log2psize, int *errp)
{
	vrtx_hdesc_t hdesc;
	int hid;

	if (heapaddr)
		fprintf(stderr,
			"sc_hcreate() - heapaddr parameter ignored from user-space context\n");

	*errp = XENOMAI_SKINCALL3(__vrtx_muxid,
				  __vrtx_hcreate, heapsize, log2psize, &hdesc);
	if (*errp)
		return 0;

	hid = hdesc.hid;
	*errp = __map_heap_memory(&hdesc);

	if (*errp)
		/* If the mapping fails, make sure we don't leave a dandling
		   heap in kernel space -- remove it. */
		XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_hdelete, hid, 1);

	return hid;
}

void sc_hdelete(int hid, int opt, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_hdelete, hid, opt);
}

char *sc_halloc(int hid, unsigned long size, int *errp)
{
	char *buf = NULL;
	*errp = XENOMAI_SKINCALL3(__vrtx_muxid, __vrtx_halloc, hid, size, &buf);
	return buf;
}

void sc_hfree(int hid, char *buf, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_hfree, hid, buf);
}

void sc_hinquiry(int info[3], int hid, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_hinquiry, info, hid);
}
