/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2008, 2009 Jan Kiszka <jan.kiszka@siemens.com>.
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_SYS_SYNCH_H
#define _COBALT_UAPI_SYS_SYNCH_H

#include <cobalt/uapi/sys/types.h>

/* Creation flags */
#define XNSYNCH_FIFO    0x0
#define XNSYNCH_PRIO    0x1
#define XNSYNCH_NOPIP   0x0
#define XNSYNCH_PIP     0x2
#define XNSYNCH_DREORD  0x4
#define XNSYNCH_OWNER   0x8

#define XNSYNCH_FLCLAIM XN_HANDLE_SPARE3 /* Corresponding bit in fast lock */

#define xnhandle_mask_spare(handle)  ((handle) & ~XN_HANDLE_SPARE_MASK)
#define xnhandle_test_spare(handle, bits)  (!!((handle) & (bits)))
#define xnhandle_set_spare(handle, bits) \
	do { (handle) |= (bits); } while (0)
#define xnhandle_clear_spare(handle, bits) \
	do { (handle) &= ~(bits); } while (0)

/* Fast lock API */
static inline int xnsynch_fast_owner_check(atomic_long_t *fastlock,
					   xnhandle_t ownerh)
{
	return (xnhandle_mask_spare(atomic_long_read(fastlock)) == ownerh) ?
		0 : -EPERM;
}

static inline int xnsynch_fast_acquire(atomic_long_t *fastlock,
				       xnhandle_t new_ownerh)
{
	xnhandle_t h;

	h = atomic_long_cmpxchg(fastlock, XN_NO_HANDLE, new_ownerh);
	if (h != XN_NO_HANDLE) {
		if (xnhandle_mask_spare(h) == new_ownerh)
			return -EBUSY;

		return -EAGAIN;
	}

	return 0;
}

static inline int xnsynch_fast_release(atomic_long_t *fastlock,
				       xnhandle_t cur_ownerh)
{
	return (atomic_long_cmpxchg(fastlock, cur_ownerh, XN_NO_HANDLE) ==
		cur_ownerh);
}

#endif /* !_COBALT_UAPI_SYS_SYNCH_H */
