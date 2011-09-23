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
#include "cond.h"
#include "timer.h"
#include "mutex.h"

/*
 * XXX: Alchemy condvars are paired with Alchemy mutex objects, so we
 * must rely on POSIX condvars directly.
 */

struct cluster alchemy_cond_table;

static struct alchemy_cond *
__get_alchemy_cond(struct alchemy_cond *ccb, int *err_r)
{
	int ret;

	if (ccb->magic == ~cond_magic)
		goto dead_handle;

	if (ccb->magic != cond_magic)
		goto bad_handle;

	ret = __RT(pthread_mutex_lock(&ccb->safe));
	if (ret)
		goto bad_handle;

	/* Recheck under lock. */
	if (ccb->magic == cond_magic)
		return ccb;

dead_handle:
	/* Removed under our feet. */
	*err_r = -EIDRM;
	return NULL;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

static struct alchemy_cond *get_alchemy_cond(RT_COND *cond, int *err_r)
{
	struct alchemy_cond *ccb;

	if (cond == NULL || ((intptr_t)cond & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	ccb = mainheap_deref(cond->handle, struct alchemy_cond);
	if (ccb == NULL || ((intptr_t)ccb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	return __get_alchemy_cond(ccb, err_r);

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

static inline void put_alchemy_cond(struct alchemy_cond *ccb)
{
	__RT(pthread_mutex_unlock(&ccb->safe));
}

static struct alchemy_cond *find_alchemy_cond(RT_COND *cond, int *err_r)
{
	struct alchemy_cond *ccb;

	if (cond == NULL || ((intptr_t)cond & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	ccb = mainheap_deref(cond->handle, struct alchemy_cond);
	if (ccb == NULL || ((intptr_t)ccb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (ccb->magic == ~cond_magic) {
		*err_r = -EIDRM;
		return NULL;
	}

	if (ccb->magic == cond_magic)
		return ccb;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

int rt_cond_create(RT_COND *cond, const char *name)
{
	pthread_mutexattr_t mattr;
	struct alchemy_cond *ccb;
	pthread_condattr_t cattr;
	struct service svc;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	ccb = xnmalloc(sizeof(*ccb));
	if (ccb == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	strncpy(ccb->name, name, sizeof(ccb->name));
	ccb->name[sizeof(ccb->name) - 1] = '\0';
	ccb->nwaiters = 0;

	if (cluster_addobj(&alchemy_cond_table, ccb->name, &ccb->cobj)) {
		xnfree(ccb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute));
	__RT(pthread_mutex_init(&ccb->safe, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));

	__RT(pthread_condattr_init(&cattr));
	__RT(pthread_condattr_setpshared(&cattr, mutex_scope_attribute));
	__RT(pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE));
	__RT(pthread_cond_init(&ccb->cond, &cattr));
	__RT(pthread_condattr_destroy(&cattr));
	ccb->magic = cond_magic;
	cond->handle = mainheap_ref(ccb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

int rt_cond_delete(RT_COND *cond)
{
	struct alchemy_cond *ccb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	ccb = get_alchemy_cond(cond, &ret);
	if (ccb == NULL)
		goto out;

	ret = -__RT(pthread_cond_destroy(&ccb->cond));
	if (ret) {
		if (ret == -EBUSY)
			put_alchemy_cond(ccb);
		goto out;
	}

	ccb->magic = ~cond_magic;
	put_alchemy_cond(ccb);
	cluster_delobj(&alchemy_cond_table, &ccb->cobj);
	__RT(pthread_mutex_destroy(&ccb->safe));
	xnfree(ccb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_cond_signal(RT_COND *cond)
{
	struct alchemy_cond *ccb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	ccb = find_alchemy_cond(cond, &ret);
	if (ccb == NULL)
		goto out;

	ret = -__RT(pthread_cond_signal(&ccb->cond));
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_cond_broadcast(RT_COND *cond)
{
	struct alchemy_cond *ccb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	ccb = find_alchemy_cond(cond, &ret);
	if (ccb == NULL)
		goto out;

	ret = -__RT(pthread_cond_broadcast(&ccb->cond));
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_cond_wait_until(RT_COND *cond, RT_MUTEX *mutex,
		       RTIME timeout)
{
	struct alchemy_mutex *mcb;
	struct alchemy_cond *ccb;
	struct service svc;
	struct timespec ts;
	int ret = 0;

	if (timeout == TM_NONBLOCK)
		return -EWOULDBLOCK;

	COPPERPLATE_PROTECT(svc);

	ccb = get_alchemy_cond(cond, &ret);
	if (ccb == NULL)
		goto out;

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto unlock;

	ccb->nwaiters++;
	put_alchemy_cond(ccb);

	if (timeout != TM_INFINITE) {
		clockobj_ticks_to_timeout(&alchemy_clock, timeout, &ts);
		ret = -__RT(pthread_cond_timedwait(&ccb->cond, &mcb->lock, &ts));
	} else
		ret = -__RT(pthread_cond_wait(&ccb->cond, &mcb->lock));

	/*
	 * Be cautious, grab the internal safe lock again to update
	 * the control block.
	 */
	ccb = __get_alchemy_cond(ccb, &ret);
	if (ccb == NULL)
		goto out;

	ccb->nwaiters--;
unlock:
	put_alchemy_cond(ccb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_cond_wait(RT_COND *cond, RT_MUTEX *mutex,
		 RTIME timeout)
{
	struct service svc;
	ticks_t now;

	if (timeout != TM_INFINITE && timeout != TM_NONBLOCK) {
		COPPERPLATE_PROTECT(svc);
		clockobj_get_time(&alchemy_clock, &now, NULL);
		COPPERPLATE_UNPROTECT(svc);
		timeout += now;
	}

	return rt_cond_wait_until(cond, mutex, timeout);
}

int rt_cond_inquire(RT_COND *cond, RT_COND_INFO *info)
{
	struct alchemy_cond *ccb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	ccb = get_alchemy_cond(cond, &ret);
	if (ccb == NULL)
		goto out;

	strcpy(info->name, ccb->name);
	info->nwaiters = ccb->nwaiters;
	put_alchemy_cond(ccb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}
