/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <copperplate/heapobj.h>
#include <copperplate/panic.h>
#include <copperplate/cluster.h>
#include <copperplate/clockobj.h>
#include "reference.h"
#include "task.h"
#include "sem.h"
#include "tm.h"
#include <psos/psos.h>

#define sem_magic	0x8181fbfb

struct cluster psos_sem_table;

static struct psos_sem *get_sem_from_id(u_long smid, int *err_r)
{
	struct psos_sem *sem = mainheap_deref(smid, struct psos_sem);

	if (sem == NULL || ((uintptr_t)sem & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (sem->magic == sem_magic)
		return sem;

	if (sem->magic == ~sem_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((sem->magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static void sem_finalize(struct syncobj *sobj)
{
	struct psos_sem *sem = container_of(sobj, struct psos_sem, sobj);
	xnfree(sem);
}
fnref_register(libpsos, sem_finalize);

u_long sm_create(const char *name,
		 u_long count, u_long flags, u_long *smid_r)
{
	struct psos_sem *sem;
	struct service svc;
	int sobj_flags = 0;

	COPPERPLATE_PROTECT(svc);

	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return ERR_NOSCB;
	}

	strncpy(sem->name, name, sizeof(sem->name));
	sem->name[sizeof(sem->name) - 1] = '\0';

	if (cluster_addobj(&psos_sem_table, sem->name, &sem->cobj)) {
		warning("duplicate semaphore name: %s", sem->name);
		/* Make sure we won't un-hash the previous one. */
		strcpy(sem->name, "(dup)");
	}

	if (flags & SM_PRIOR)
		sobj_flags = SYNCOBJ_PRIO;

	sem->magic = sem_magic;
	sem->value = count;
	syncobj_init(&sem->sobj, sobj_flags,
		     fnref_put(libpsos, sem_finalize));
	*smid_r = mainheap_ref(sem, u_long);

	COPPERPLATE_UNPROTECT(svc);

	return SUCCESS;
}

u_long sm_delete(u_long smid)
{
	struct syncstate syns;
	struct psos_sem *sem;
	struct service svc;
	int ret;

	sem = get_sem_from_id(smid, &ret);
	if (sem == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);

	if (syncobj_lock(&sem->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	cluster_delobj(&psos_sem_table, &sem->cobj);
	sem->magic = ~sem_magic; /* Prevent further reference. */
	ret = syncobj_destroy(&sem->sobj, &syns);
	if (ret)
		ret = ERR_TATSDEL;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

u_long sm_ident(const char *name, u_long node, u_long *smid_r)
{
	struct clusterobj *cobj;
	struct psos_sem *sem;
	struct service svc;

	if (node)
		return ERR_NODENO;

	COPPERPLATE_PROTECT(svc);
	cobj = cluster_findobj(&psos_sem_table, name);
	COPPERPLATE_UNPROTECT(svc);
	if (cobj == NULL)
		return ERR_OBJNF;

	sem = container_of(cobj, struct psos_sem, cobj);
	*smid_r = mainheap_ref(sem, u_long);

	return SUCCESS;
}

u_long sm_p(u_long smid, u_long flags, u_long timeout)
{
	struct timespec ts, *timespec;
	struct syncstate syns;
	struct psos_sem *sem;
	struct service svc;
	int ret = SUCCESS;

	sem = get_sem_from_id(smid, &ret);
	if (sem == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);

	if (syncobj_lock(&sem->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if (--sem->value >= 0)
		goto done;

	if (flags & SM_NOWAIT) {
		sem->value++;
		ret = ERR_NOSEM;
		goto done;
	}

	if (timeout != 0) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&psos_clock, timeout, timespec);
	} else
		timespec = NULL;

	ret = syncobj_pend(&sem->sobj, timespec, &syns);
	if (ret) {
		if (ret == -EIDRM)
			return ERR_SKILLD;

		sem->value++;	/* Fix up semaphore count. */

		if (ret == -ETIMEDOUT)
			ret = ERR_TIMEOUT;
		/*
		 * There is no explicit flush operation on pSOS
		 * semaphores, only an implicit one through deletion.
		 */
	}
done:
	syncobj_unlock(&sem->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

u_long sm_v(u_long smid)
{
	struct syncstate syns;
	struct psos_sem *sem;
	struct service svc;
	int ret = SUCCESS;

	sem = get_sem_from_id(smid, &ret);
	if (sem == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);

	if (syncobj_lock(&sem->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if (++sem->value <= 0)
		syncobj_post(&sem->sobj);

	syncobj_unlock(&sem->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}
