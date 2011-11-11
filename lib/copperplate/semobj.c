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

#include <assert.h>
#include <errno.h>
#include "copperplate/threadobj.h"
#include "copperplate/semobj.h"
#include "copperplate/debug.h"

#ifdef CONFIG_XENO_COBALT

int semobj_init(struct semobj *smobj, int flags, int value,
		fnref_type(void (*)(struct semobj *smobj)) finalizer)
{
	int ret, sem_flags = sem_scope_attribute|SEM_REPORT;

	if ((flags & SEMOBJ_PRIO) == 0)
		sem_flags |= SEM_FIFO;

	if (flags & SEMOBJ_PULSE)
		sem_flags |= SEM_PULSE;

	if (flags & SEMOBJ_WARNDEL)
		sem_flags |= SEM_WARNDEL;

	ret = sem_init_np(&smobj->core.sem, sem_flags, value);
	if (ret)
		return __bt(-errno);

	smobj->finalizer = finalizer;

	return 0;
}

int semobj_destroy(struct semobj *smobj)
{
	void (*finalizer)(struct semobj *smobj);
	int ret;

	ret = __RT(sem_destroy(&smobj->core.sem));
	if (ret < 0)
		return errno == EINVAL ? -EIDRM : -errno;
	/*
	 * All waiters have been unblocked with EINVAL, and therefore
	 * won't touch this object anymore. We can finalize it
	 * immediately.
	 */
	fnref_get(finalizer, smobj->finalizer);
	finalizer(smobj);

	return ret;
}

int semobj_post(struct semobj *smobj)
{
	int ret;

	ret = __RT(sem_post(&smobj->core.sem));
	if (ret)
		return errno == EINVAL ? -EIDRM : -errno;

	return 0;
}

int semobj_broadcast(struct semobj *smobj)
{
	int ret;

	ret = sem_broadcast_np(&smobj->core.sem);
	if (ret)
		return errno == EINVAL ? -EIDRM : -errno;

	return 0;
}

int semobj_wait(struct semobj *smobj, struct timespec *timeout)
{
	int ret;

	if (timeout == NULL)
		ret = __RT(sem_wait(&smobj->core.sem));
	else if (timeout->tv_sec == 0 && timeout->tv_nsec == 0)
		ret = __RT(sem_trywait(&smobj->core.sem));
	else
		ret = __RT(sem_timedwait(&smobj->core.sem, timeout));

	if (ret)
		return errno == EINVAL ? -EIDRM : -errno;

	return 0;
}

int semobj_getvalue(struct semobj *smobj, int *sval)
{
	int ret;

	ret = __RT(sem_getvalue(&smobj->core.sem, sval));
	if (ret)
		return errno == EINVAL ? -EIDRM : -errno;

	return 0;
}

#else /* CONFIG_XENO_MERCURY */

static void semobj_finalize(struct syncobj *sobj)
{
	struct semobj *smobj = container_of(sobj, struct semobj, core.sobj);
	void (*finalizer)(struct semobj *smobj);

	fnref_get(finalizer, smobj->finalizer);
	finalizer(smobj);
}
fnref_register(libcopperplate, semobj_finalize);

int semobj_init(struct semobj *smobj, int flags, int value,
		fnref_type(void (*)(struct semobj *smobj)) finalizer)
{
	int sobj_flags = 0;

	if (flags & SEMOBJ_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	/*
	 * We need a trampoline for finalizing a semobj, to escalate
	 * from a basic syncobj we receive to the semobj container.
	 */
	syncobj_init(&smobj->core.sobj, sobj_flags,
		     fnref_put(libcopperplate, semobj_finalize));
	smobj->core.flags = flags;
	smobj->core.value = value;
	smobj->finalizer = finalizer;

	return 0;
}

int semobj_destroy(struct semobj *smobj)
{
	struct syncstate syns;

	if (syncobj_lock(&smobj->core.sobj, &syns))
		return -EINVAL;

	return syncobj_destroy(&smobj->core.sobj, &syns);
}

int semobj_post(struct semobj *smobj)
{
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&smobj->core.sobj, &syns);
	if (ret)
		return ret;

	if (++smobj->core.value <= 0)
		syncobj_post(&smobj->core.sobj);
	else if (smobj->core.flags & SEMOBJ_PULSE)
		smobj->core.value = 0;

	syncobj_unlock(&smobj->core.sobj, &syns);

	return 0;
}

int semobj_broadcast(struct semobj *smobj)
{
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&smobj->core.sobj, &syns);
	if (ret)
		return ret;

	if (smobj->core.value < 0) {
		smobj->core.value = 0;
		syncobj_flush(&smobj->core.sobj, SYNCOBJ_BROADCAST);
	}

	syncobj_unlock(&smobj->core.sobj, &syns);

	return 0;
}

int semobj_wait(struct semobj *smobj, struct timespec *timeout)
{
	struct syncstate syns;
	int ret = 0;

	ret = syncobj_lock(&smobj->core.sobj, &syns);
	if (ret)
		return ret;

	if (--smobj->core.value >= 0)
		goto done;

	if (timeout &&
	    timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
		smobj->core.value++;
		ret = -EWOULDBLOCK;
		goto done;
	}

	if (!threadobj_current_p()) {
		ret = -EPERM;
		goto done;
	}

	ret = syncobj_pend(&smobj->core.sobj, timeout, &syns);
	if (ret) {
		/*
		 * -EIDRM means that the semaphore has been deleted,
		 * so we bail out immediately and don't attempt to
		 * access that stale object in any way.
		 */
		if (ret == -EIDRM)
			return ret;

		smobj->core.value++; /* Fix up semaphore count. */
	}
done:
	syncobj_unlock(&smobj->core.sobj, &syns);

	return ret;
}

int semobj_getvalue(struct semobj *smobj, int *sval)
{
	struct syncstate syns;

	if (syncobj_lock(&smobj->core.sobj, &syns))
		return -EINVAL;

	*sval = smobj->core.value;

	syncobj_unlock(&smobj->core.sobj, &syns);

	return 0;
}

#endif /* CONFIG_XENO_MERCURY */
