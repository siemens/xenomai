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
#include <psos+/psos.h>
#include <psos+/long_names.h>

extern int __psos_muxid;

struct rninfo {
	u_long rnid;
	u_long allocsz;
	void *rncb;
	u_long mapsize;
	u_long area;
};

void *xeno_map_heap(struct xnheap_desc *hd);

static int __map_heap_memory(const struct rninfo *rnip)
{
	struct xnheap_desc hd;
	caddr_t mapbase;

	hd.handle = (unsigned long)rnip->rncb;
	hd.size = rnip->mapsize;
	hd.area = rnip->area;
	mapbase = xeno_map_heap(&hd);
	if (mapbase == MAP_FAILED)
		return -errno;

	return XENOMAI_SKINCALL2(__psos_muxid, __psos_rn_bind,
				 rnip->rnid, mapbase);
}

u_long rn_create(const char name[4],
		 void *rnaddr,
		 u_long rnsize,
		 u_long usize, u_long flags, u_long *rnid, u_long *allocsz)
{
	struct rninfo rninfo;
	char short_name[5];
	struct {
		u_long rnsize;
		u_long usize;
		u_long flags;
	} sizeopt;
	u_long err;

	name = __psos_maybe_short_name(short_name, name);

	if (rnaddr)
		fprintf(stderr,
			"rn_create() - rnaddr parameter ignored from user-space context\n");

	sizeopt.rnsize = rnsize;
	sizeopt.usize = usize;
	sizeopt.flags = flags;

	err = XENOMAI_SKINCALL3(__psos_muxid,
				__psos_rn_create, name, &sizeopt, &rninfo);
	if (err)
		return err;

	err = __map_heap_memory(&rninfo);

	if (err) {
		/* If the mapping fails, make sure we don't leave a dandling
		   heap in kernel space -- remove it. */
		XENOMAI_SKINCALL1(__psos_muxid, __psos_rn_delete, rninfo.rnid);
		return err;
	}

	*rnid = rninfo.rnid;
	*allocsz = rninfo.allocsz;

	return SUCCESS;
}

u_long rn_delete(u_long rnid)
{
	return XENOMAI_SKINCALL1(__psos_muxid, __psos_rn_delete, rnid);
}

u_long rn_getseg(u_long rnid,
		 u_long size, u_long flags, u_long timeout, void **segaddr)
{
	return XENOMAI_SKINCALL5(__psos_muxid, __psos_rn_getseg,
				 rnid, size, flags, timeout, segaddr);
}

u_long rn_retseg(u_long rnid, void *chunk)
{
	return XENOMAI_SKINCALL2(__psos_muxid, __psos_rn_retseg,
				 rnid, chunk);
}

u_long rn_ident(const char name[4], u_long *rnid_r)
{
	char short_name[5];

	name = __psos_maybe_short_name(short_name, name);

	return XENOMAI_SKINCALL2(__psos_muxid, __psos_rn_ident, name, rnid_r);
}
