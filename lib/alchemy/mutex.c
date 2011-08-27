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
#include "mutex.h"
#include "timer.h"

struct cluster alchemy_mutex_table;

static struct alchemy_mutex *find_alchemy_mutex(RT_MUTEX *mutex, int *err_r)
{
	struct alchemy_mutex *mcb;

	if (mutex == NULL || ((intptr_t)mutex & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	mcb = mainheap_deref(mutex->handle, struct alchemy_mutex);
	if (mcb == NULL || ((intptr_t)mcb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (mcb->magic == ~mutex_magic) {
		*err_r = -EIDRM;
		return NULL;
	}

	if (mcb->magic == mutex_magic)
		return mcb;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

int rt_mutex_create(RT_MUTEX *mutex, const char *name)
{
	struct alchemy_mutex *mcb;
	pthread_mutexattr_t mattr;
	struct service svc;

	COPPERPLATE_PROTECT(svc);

	mcb = xnmalloc(sizeof(*mcb));
	if (mcb == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	strncpy(mcb->name, name, sizeof(mcb->name));
	mcb->name[sizeof(mcb->name) - 1] = '\0';
	mcb->owner = NULL;
	mcb->nwaiters = 0;

	if (cluster_addobj(&alchemy_mutex_table, mcb->name, &mcb->cobj)) {
		xnfree(mcb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE));
#ifdef CONFIG_XENO_MERCURY
	pthread_mutexattr_setrobust_np(&mattr, PTHREAD_MUTEX_ROBUST_NP);
#endif
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute));
	__RT(pthread_mutex_init(&mcb->lock, &mattr));
	__RT(pthread_mutex_init(&mcb->hold, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));
	mcb->magic = mutex_magic;
	mutex->handle = mainheap_ref(mcb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

int rt_mutex_delete(RT_MUTEX *mutex)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	ret = -__RT(pthread_mutex_lock(&mcb->hold));
	if (ret)
		goto out;

	ret = -__RT(pthread_mutex_destroy(&mcb->lock));
	if (ret)
		goto out;

	__RT(pthread_mutex_unlock(&mcb->hold));
	__RT(pthread_mutex_destroy(&mcb->hold));

	cluster_delobj(&alchemy_mutex_table, &mcb->cobj);
	mcb->magic = ~mutex_magic; /* Prevent further reference. */
	xnfree(mcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_acquire_until(RT_MUTEX *mutex, RTIME timeout)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	struct timespec ts;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	if (timeout == TM_NONBLOCK) {
		ret = -__RT(pthread_mutex_trylock(&mcb->lock));
		goto done;
	}

	if (timeout == TM_INFINITE) {
		ret = -__RT(pthread_mutex_lock(&mcb->lock));
		goto done;
	}

	mcb->nwaiters++;
	__clockobj_ticks_to_timeout(&alchemy_clock, CLOCK_REALTIME, timeout, &ts);
	ret = -__RT(pthread_mutex_timedlock(&mcb->lock, &ts));
	mcb->nwaiters--;
	/* FIXME: smp_mb() would be safer */
done:
	switch (ret) {
	case -ENOTRECOVERABLE:
		ret = -EOWNERDEAD;
	case -EOWNERDEAD:
		warning("owner of mutex 0x%x died", mutex->handle);
		break;
	case 0:
		mcb->owner = threadobj_current();
	}
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_acquire(RT_MUTEX *mutex, RTIME timeout)
{
	ticks_t now;

	if (timeout != TM_INFINITE && timeout != TM_NONBLOCK) {
		clockobj_get_time(&alchemy_clock, &now, NULL);
		timeout += now;
	}

	return rt_mutex_acquire_until(mutex, timeout);
}

int rt_mutex_release(RT_MUTEX *mutex)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	ret = -__RT(pthread_mutex_unlock(&mcb->lock));
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	ret = -__RT(pthread_mutex_lock(&mcb->hold));
	if (ret)
		goto out;

	ret = -__RT(pthread_mutex_trylock(&mcb->lock));
	if (ret) {
		if (ret != -EBUSY)
			goto done;
		info->locked = 1;
		strcpy(info->owner, threadobj_get_name(mcb->owner));
	} else {
		__RT(pthread_mutex_unlock(&mcb->lock));
		info->locked = 0;
	}

	info->nwaiters = mcb->nwaiters;
	strcpy(info->name, mcb->name);
done:
	__RT(pthread_mutex_unlock(&mcb->hold));
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}
