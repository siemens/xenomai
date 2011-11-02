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
#include "internal.h"
#include "mutex.h"
#include "timer.h"
#include "task.h"

/*
 * XXX: we can't obtain priority inheritance with syncobj, so we have
 * to base this code directly over the POSIX layer.
 */

struct syncluster alchemy_mutex_table;

static struct alchemy_namegen mutex_namegen = {
	.prefix = "mutex",
	.length = sizeof ((struct alchemy_mutex *)0)->name,
};

struct alchemy_mutex *find_alchemy_mutex(RT_MUTEX *mutex, int *err_r)
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

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	mcb = xnmalloc(sizeof(*mcb));
	if (mcb == NULL) {
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	__alchemy_build_name(mcb->name, name, &mutex_namegen);
	mcb->owner = no_alchemy_task;

	if (syncluster_addobj(&alchemy_mutex_table, mcb->name, &mcb->cobj)) {
		xnfree(mcb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute));
	__RT(pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE));
#ifdef CONFIG_XENO_MERCURY
	pthread_mutexattr_setrobust_np(&mattr, PTHREAD_MUTEX_ROBUST_NP);
#endif
	__RT(pthread_mutex_init(&mcb->lock, &mattr));
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

	ret = -__RT(pthread_mutex_destroy(&mcb->lock));
	if (ret)
		goto out;

	mcb->magic = ~mutex_magic;
	syncluster_delobj(&alchemy_mutex_table, &mcb->cobj);
	xnfree(mcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_acquire_until(RT_MUTEX *mutex, RTIME timeout)
{
	struct alchemy_mutex *mcb;
	struct alchemy_task *tcb;
	struct timespec ts;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	/* Try the fast path first. */
	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	/*
	 * We found the mutex, but locklessly: let the POSIX layer
	 * check for object existence.
	 */
	ret = -__RT(pthread_mutex_trylock(&mcb->lock));
	if (ret == 0 || ret != -EBUSY || timeout == TM_NONBLOCK)
		goto done;

	/* Slow path. */
	if (timeout == TM_INFINITE) {
		ret = -__RT(pthread_mutex_lock(&mcb->lock));
		goto done;
	}

	/*
	 * What a mess: we want all our timings to be based on
	 * CLOCK_COPPERPLATE, but pthread_mutex_timedlock() is
	 * implicitly based on CLOCK_REALTIME, so we need to translate
	 * the user timeout into something POSIX understands.
	 */
	clockobj_ticks_to_clock(&alchemy_clock, timeout, CLOCK_REALTIME, &ts);
	ret = -__RT(pthread_mutex_timedlock(&mcb->lock, &ts));
done:
	switch (ret) {
	case -ENOTRECOVERABLE:
		ret = -EOWNERDEAD;
	case -EOWNERDEAD:
		warning("owner of mutex 0x%x died", mutex->handle);
		break;
	case -EBUSY:
		/*
		 * Remap EBUSY -> EWOULDBLOCK: not very POSIXish, but
		 * consistent with similar cases in the Alchemy API.
		 */
		ret = -EWOULDBLOCK;
		break;
	case 0:
		tcb = alchemy_task_current();
		mcb->owner.handle = mainheap_ref(tcb, uintptr_t);
	}
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_acquire(RT_MUTEX *mutex, RTIME timeout)
{
	timeout = __alchemy_rel2abs_timeout(timeout);
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

	/* Let the POSIX layer check for object existence. */
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

	ret = -__RT(pthread_mutex_trylock(&mcb->lock));
	if (ret) {
		if (ret != -EBUSY)
			goto out;
		info->owner = mcb->owner;
		ret = 0;
	} else {
		__RT(pthread_mutex_unlock(&mcb->lock));
		info->owner = no_alchemy_task;
	}

	strcpy(info->name, mcb->name);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_bind(RT_MUTEX *mutex,
		  const char *name, RTIME timeout)
{
	return __alchemy_bind_object(name,
				     &alchemy_mutex_table,
				     timeout,
				     offsetof(struct alchemy_mutex, cobj),
				     &mutex->handle);
}

int rt_mutex_unbind(RT_MUTEX *mutex)
{
	mutex->handle = 0;
	return 0;
}
