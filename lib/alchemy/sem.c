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

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "reference.h"
#include "sem.h"
#include "timer.h"

struct cluster alchemy_sem_table;

static struct alchemy_sem *get_alchemy_sem(RT_SEM *sem,
					   struct syncstate *syns, int *err_r)
{
	struct alchemy_sem *scb;

	if (sem == NULL || ((intptr_t)sem & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	scb = mainheap_deref(sem->handle, struct alchemy_sem);
	if (scb == NULL || ((intptr_t)scb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (scb->magic == ~sem_magic)
		goto dead_handle;

	if (scb->magic != sem_magic)
		goto bad_handle;

	if (syncobj_lock(&scb->sobj, syns))
		goto bad_handle;

	/* Recheck under lock. */
	if (scb->magic == sem_magic)
		return scb;

dead_handle:
	/* Removed under our feet. */
	*err_r = -EIDRM;
	return NULL;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

static inline void put_alchemy_sem(struct alchemy_sem *scb,
				   struct syncstate *syns)
{
	syncobj_unlock(&scb->sobj, syns);
}

static void sem_finalize(struct syncobj *sobj)
{
	struct alchemy_sem *scb = container_of(sobj, struct alchemy_sem, sobj);
	xnfree(scb);
}
fnref_register(libalchemy, sem_finalize);

int rt_sem_create(RT_SEM *sem, const char *name,
		  unsigned long icount, int mode)
{
	struct alchemy_sem *scb;
	struct service svc;
	int sobj_flags = 0;

	if (threadobj_async_p())
		return -EPERM;

	if ((mode & S_PULSE) && icount > 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	scb = xnmalloc(sizeof(*scb));
	if (scb == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	strncpy(scb->name, name, sizeof(scb->name));
	scb->name[sizeof(scb->name) - 1] = '\0';

	if (cluster_addobj(&alchemy_sem_table, scb->name, &scb->cobj)) {
		xnfree(scb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	if (mode & S_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	scb->magic = sem_magic;
	scb->value = icount;
	scb->mode = mode;
	syncobj_init(&scb->sobj, sobj_flags,
		     fnref_put(libalchemy, sem_finalize));
	sem->handle = mainheap_ref(scb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

int rt_sem_delete(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	scb = get_alchemy_sem(sem, &syns, &ret);
	if (scb == NULL)
		goto out;

	cluster_delobj(&alchemy_sem_table, &scb->cobj);
	scb->magic = ~sem_magic; /* Prevent further reference. */
	syncobj_destroy(&scb->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_p_until(RT_SEM *sem, RTIME timeout)
{
	struct timespec ts, *timespec;
	struct alchemy_sem *scb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	scb = get_alchemy_sem(sem, &syns, &ret);
	if (scb == NULL)
		goto out;

	if (--scb->value >= 0)
		goto done;

	if (timeout == TM_NONBLOCK) {
		scb->value++;
		ret = -EWOULDBLOCK;
		goto done;
	}

	if (threadobj_async_p()) {
		ret = -EPERM;
		goto done;
	}

	if (timeout != TM_INFINITE) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&alchemy_clock, timeout, timespec);
	} else
		timespec = NULL;

	ret = syncobj_pend(&scb->sobj, timespec, &syns);
	if (ret) {
		/*
		 * -EIDRM means that the semaphore has been deleted,
		 * so we bail out immediately and don't attempt to
		 * access that stale object in any way.
		 */
		if (ret == -EIDRM)
			goto out;

		scb->value++;	/* Fix up semaphore count. */
	}
done:
	put_alchemy_sem(scb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_p(RT_SEM *sem, RTIME timeout)
{
	struct service svc;
	ticks_t now;

	if (threadobj_async_p())
		return -EPERM;

	if (timeout != TM_INFINITE && timeout != TM_NONBLOCK) {
		COPPERPLATE_PROTECT(svc);
		clockobj_get_time(&alchemy_clock, &now, NULL);
		COPPERPLATE_UNPROTECT(svc);
		timeout += now;
	}

	return rt_sem_p_until(sem, timeout);
}

int rt_sem_v(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	scb = get_alchemy_sem(sem, &syns, &ret);
	if (scb == NULL)
		goto out;

	if (++scb->value <= 0)
		syncobj_post(&scb->sobj);
	else if (scb->mode & S_PULSE)
		scb->value = 0;

	put_alchemy_sem(scb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_broadcast(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	scb = get_alchemy_sem(sem, &syns, &ret);
	if (scb == NULL)
		goto out;

	if (scb->value < 0) {
		scb->value = 0;
		syncobj_flush(&scb->sobj, SYNCOBJ_BROADCAST);
	}

	put_alchemy_sem(scb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
{
	struct alchemy_sem *scb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	scb = get_alchemy_sem(sem, &syns, &ret);
	if (scb == NULL)
		goto out;

	info->count = scb->value < 0 ? 0 : scb->value;
	info->nwaiters = -scb->value;
	strcpy(info->name, scb->name);

	put_alchemy_sem(scb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}
