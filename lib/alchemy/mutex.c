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

DEFINE_LOOKUP(mutex, RT_MUTEX);

int rt_mutex_create(RT_MUTEX *mutex, const char *name)
{
	struct alchemy_mutex *mcb;
	pthread_mutexattr_t mattr;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	mcb = xnmalloc(sizeof(*mcb));
	if (mcb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	alchemy_build_name(mcb->name, name, &mutex_namegen);
	mcb->owner = no_alchemy_task;
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

	if (syncluster_addobj(&alchemy_mutex_table, mcb->name, &mcb->cobj)) {
		xnfree(mcb);
		ret = -EEXIST;
	} else
		mutex->handle = mainheap_ref(mcb, uintptr_t);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_mutex_delete(RT_MUTEX *mutex)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
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

int rt_mutex_acquire_timed(RT_MUTEX *mutex,
			   const struct timespec *abs_timeout)
{
	struct alchemy_task *current;
	struct alchemy_mutex *mcb;
	struct timespec ts;
	int ret = 0;

	/* This must be an alchemy task. */
	current = alchemy_task_current();
	if (current == NULL)
		return -EPERM;

	/*
	 * Try the fast path first. Note that we don't need any
	 * protected section here: the caller should have provided for
	 * it.
	 */
	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		return ret;

	/*
	 * We found the mutex, but locklessly: let the POSIX layer
	 * check for object existence.
	 */
	ret = -__RT(pthread_mutex_trylock(&mcb->lock));
	if (ret == 0 || ret != -EBUSY || alchemy_poll_mode(abs_timeout))
		goto done;

	/* Slow path. */
	if (abs_timeout == NULL) {
		ret = -__RT(pthread_mutex_lock(&mcb->lock));
		goto done;
	}

	/*
	 * What a mess: we want all our timings to be based on
	 * CLOCK_COPPERPLATE, but pthread_mutex_timedlock() is
	 * implicitly based on CLOCK_REALTIME, so we need to translate
	 * the user timeout into something POSIX understands.
	 */
	clockobj_convert_clocks(&alchemy_clock, abs_timeout, CLOCK_REALTIME, &ts);
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
		mcb->owner.handle = mainheap_ref(current, uintptr_t);
	}

	return ret;
}

int rt_mutex_release(RT_MUTEX *mutex)
{
	struct alchemy_mutex *mcb;
	int ret = 0;

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		return ret;

	/* Let the POSIX layer check for object existence. */
	return -__RT(pthread_mutex_unlock(&mcb->lock));
}

int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
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
	return alchemy_bind_object(name,
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
