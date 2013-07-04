/*
 * Copyright (C) 2009 Wolfgang Mauerer <wolfgang.mauerer@siemens.com>.
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
#ifndef _COBALT_KERNEL_UAPI_VDSO_H
#define _COBALT_KERNEL_UAPI_VDSO_H

#include <cobalt/kernel/uapi/urw.h>

struct xnvdso_hostrt_data {
	short live;
	urw_t lock;
	time_t wall_time_sec;
	unsigned int wall_time_nsec;
	struct timespec wall_to_monotonic;
	unsigned long long cycle_last;
	unsigned long long mask;
	unsigned int mult;
	unsigned int shift;
};

/*
 * Data shared between Xenomai kernel/userland and the Linux
 * kernel/userland on the global semaphore heap. The features element
 * indicates which data are shared. Notice that struct xnvdso may only
 * grow, but never shrink.
 */
struct xnvdso {
	unsigned long long features;

	/* XNVDSO_FEAT_HOST_REALTIME */
	struct xnvdso_hostrt_data hostrt_data;
};

/* For each shared feature, add a flag below. */

#define XNVDSO_FEAT_HOST_REALTIME	0x0000000000000001ULL

static inline int xnvdso_test_feature(struct xnvdso *vdso,
				      unsigned long long feature)
{
	return (vdso->features & feature) != 0;
}

#endif /* !_COBALT_KERNEL_UAPI_VDSO_H */
