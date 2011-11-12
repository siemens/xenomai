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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <copperplate/lock.h>
#include <copperplate/cluster.h>
#include <copperplate/heapobj.h>
#include "internal.h"

char *alchemy_build_name(char *buf, const char *name,
			 struct alchemy_namegen *ngen)
{
	int len = ngen->length - 1, tag;

	if (name && *name) {
		strncpy(buf, name, len);
		buf[len] = '\0';
	} else {
		tag = atomic_add_fetch(ngen->serial, 1);
		snprintf(buf, len, "%s@%d", ngen->prefix, tag);
	}

	return buf;
}

RTIME alchemy_rel2abs_timeout(RTIME timeout)
{
	struct service svc;
	ticks_t now;

	if (timeout != TM_INFINITE && timeout != TM_NONBLOCK) {
		COPPERPLATE_PROTECT(svc);
		clockobj_get_time(&alchemy_clock, &now, NULL);
		COPPERPLATE_UNPROTECT(svc);
		timeout += now;
	}

	return timeout;
}

struct timespec *alchemy_get_timespec(RTIME timeout, struct timespec *tmp)
{
	if (timeout == TM_INFINITE)
		return NULL;

	if (timeout == TM_NONBLOCK) {
		tmp->tv_sec = 0;
		tmp->tv_nsec = 0;
	} else
		clockobj_ticks_to_timespec(&alchemy_clock, timeout, tmp);

	return tmp;
}

struct timespec *alchemy_get_timeout(RTIME timeout, struct timespec *tmp)
{
	if (timeout == TM_INFINITE)
		return NULL;

	if (timeout == TM_NONBLOCK) {
		tmp->tv_sec = 0;
		tmp->tv_nsec = 0;
	} else
		clockobj_ticks_to_timeout(&alchemy_clock, timeout, tmp);

	return tmp;
}

int alchemy_bind_object(const char *name, struct syncluster *sc,
			RTIME timeout,
			int offset,
			uintptr_t *handle)
{
	struct timespec ts, *timespec;
	struct clusterobj *cobj;
	struct service svc;
	void *p;
	int ret;

	COPPERPLATE_PROTECT(svc);
	timespec = alchemy_get_timeout(timeout, &ts);
	ret = syncluster_findobj(sc, name, timespec, &cobj);
	COPPERPLATE_UNPROTECT(svc);
	if (ret)
		return ret;

	p = cobj;
	p -= offset;
	*handle = mainheap_ref(p, uintptr_t);

	return 0;
}
