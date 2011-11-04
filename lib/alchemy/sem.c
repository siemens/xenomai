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
#include "internal.h"
#include "sem.h"
#include "timer.h"

struct syncluster alchemy_sem_table;

static struct alchemy_namegen sem_namegen = {
	.prefix = "sem",
	.length = sizeof ((struct alchemy_sem *)0)->name,
};

static struct alchemy_sem *find_alchemy_sem(RT_SEM *sem, int *err_r)
{
	struct alchemy_sem *scb;

	if (sem == NULL || ((intptr_t)sem & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	scb = mainheap_deref(sem->handle, struct alchemy_sem);
	if (scb == NULL || ((intptr_t)scb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (scb->magic == ~sem_magic)
		goto dead_handle;

	if (scb->magic == sem_magic)
		return scb;
bad_handle:
	*err_r = -EINVAL;
	return NULL;

dead_handle:
	/* Removed under our feet. */
	*err_r = -EIDRM;
	return NULL;
}

static void sem_finalize(struct semobj *smobj)
{
	struct alchemy_sem *scb = container_of(smobj, struct alchemy_sem, smobj);
	xnfree(scb);
}
fnref_register(libalchemy, sem_finalize);

int rt_sem_create(RT_SEM *sem, const char *name,
		  unsigned long icount, int mode)
{
	int smobj_flags = 0, ret;
	struct alchemy_sem *scb;
	struct service svc;

	if (threadobj_async_p())
		return -EPERM;

	if (mode & S_PULSE) {
		if (icount > 0)
			return -EINVAL;
		smobj_flags |= SEMOBJ_PULSE;
	}

	COPPERPLATE_PROTECT(svc);

	scb = xnmalloc(sizeof(*scb));
	if (scb == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	alchemy_build_name(scb->name, name, &sem_namegen);

	if (syncluster_addobj(&alchemy_sem_table, scb->name, &scb->cobj)) {
		xnfree(scb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	if (mode & S_PRIO)
		smobj_flags |= SEMOBJ_PRIO;

	ret = semobj_init(&scb->smobj, smobj_flags, icount,
			  fnref_put(libalchemy, sem_finalize));
	if (ret) {
		syncluster_delobj(&alchemy_sem_table, &scb->cobj);
		xnfree(scb);
		COPPERPLATE_UNPROTECT(svc);
		return ret;
	}

	scb->magic = sem_magic;
	sem->handle = mainheap_ref(scb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

int rt_sem_delete(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	syncluster_delobj(&alchemy_sem_table, &scb->cobj);
	scb->magic = ~sem_magic; /* Prevent further reference. */
	semobj_destroy(&scb->smobj);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_p_until(RT_SEM *sem, RTIME timeout)
{
	struct timespec ts, *timespec;
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	if (timeout == TM_INFINITE)
		timespec = NULL;
	else {
		timespec = &ts;
		if (timeout == TM_NONBLOCK) {
			ts.tv_sec = 0;
			ts.tv_nsec = 0;
		} else
			clockobj_ticks_to_timespec(&alchemy_clock,
						   timeout, timespec);
	}

	ret = semobj_wait(&scb->smobj, timespec);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_p(RT_SEM *sem, RTIME timeout)
{
	timeout = alchemy_rel2abs_timeout(timeout);
	return rt_sem_p_until(sem, timeout);
}

int rt_sem_v(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_post(&scb->smobj);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_broadcast(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_broadcast(&scb->smobj);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0, sval;

	COPPERPLATE_PROTECT(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_getvalue(&scb->smobj, &sval);
	if (ret)
		goto out;

	info->count = sval < 0 ? 0 : sval;
	info->nwaiters = -sval;
	strcpy(info->name, scb->name); /* <= racy. */
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_sem_bind(RT_SEM *sem,
		const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_sem_table,
				   timeout,
				   offsetof(struct alchemy_sem, cobj),
				   &sem->handle);
}

int rt_sem_unbind(RT_SEM *sem)
{
	sem->handle = 0;
	return 0;
}
