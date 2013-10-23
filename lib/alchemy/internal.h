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

#ifndef _ALCHEMY_INTERNAL_H
#define _ALCHEMY_INTERNAL_H

#include "boilerplate/ancillaries.h"
#include "timer.h"

#define DEFINE_SYNC_LOOKUP(__name, __dsctype)				\
static inline struct alchemy_ ## __name *				\
get_alchemy_ ## __name(__dsctype *desc,					\
		       struct syncstate *syns, int *err_r)		\
{									\
	struct alchemy_ ## __name *cb;					\
									\
	if (bad_pointer(desc)) {					\
		*err_r = -EINVAL;					\
		return NULL;						\
	}								\
									\
	cb = mainheap_deref(desc->handle, struct alchemy_ ## __name);	\
	if (bad_pointer(cb)) {						\
		*err_r = -EINVAL;					\
		return NULL;						\
	}								\
									\
	if (syncobj_lock(&cb->sobj, syns) ||				\
	    cb->magic != __name ## _magic) {				\
		*err_r = -EINVAL;					\
		return NULL;						\
	}								\
									\
	return cb;							\
}									\
									\
static inline								\
void put_alchemy_ ## __name(struct alchemy_ ## __name *cb,		\
			    struct syncstate *syns)			\
{									\
	syncobj_unlock(&cb->sobj, syns);				\
}

#define __DEFINE_LOOKUP(__scope, __name, __dsctype)			\
__scope struct alchemy_ ## __name *					\
find_alchemy_ ## __name(__dsctype *desc, int *err_r)			\
{									\
	struct alchemy_ ## __name *cb;					\
									\
	if (bad_pointer(desc)) {					\
		*err_r = -EINVAL;					\
		return NULL;						\
	}								\
									\
	cb = mainheap_deref(desc->handle, struct alchemy_ ## __name);	\
	if (bad_pointer(cb) || cb->magic != __name ## _magic) {		\
		*err_r = -EINVAL;					\
		return NULL;						\
	}								\
									\
	return cb;							\
}									\

#define DEFINE_LOOKUP_PRIVATE(__name, __dsctype)			\
	__DEFINE_LOOKUP(static inline, __name, __dsctype)

#define DEFINE_LOOKUP(__name, __dsctype)				\
	__DEFINE_LOOKUP(, __name, __dsctype)

struct syncluster;

int alchemy_bind_object(const char *name, struct syncluster *sc,
			RTIME timeout,
			int offset,
			uintptr_t *handle);

#endif /* !_ALCHEMY_INTERNAL_H */
