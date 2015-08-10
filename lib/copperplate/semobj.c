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
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

int semobj_init(struct semobj *smobj, int flags, int value,
		fnref_type(void (*)(struct semobj *smobj)) finalizer)
{
	int ret, sem_flags;

	sem_flags = SEM_REPORT|SEM_RAWCLOCK;
	if (sem_scope_attribute)
		sem_flags |= SEM_PSHARED;

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

void semobj_uninit(struct semobj *smobj)
{
	int ret = __RT(sem_destroy(&smobj->core.sem));
	assert(ret == 0);
	(void)ret;
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

int semobj_wait(struct semobj *smobj, const struct timespec *timeout)
{
	int ret;

	if (timeout == NULL) {
		do
			ret = __RT(sem_wait(&smobj->core.sem));
		while (ret && errno == EINTR);
	} else if (timeout->tv_sec == 0 && timeout->tv_nsec == 0)
		ret = __RT(sem_trywait(&smobj->core.sem));
	else {
		do
			ret = __RT(sem_timedwait(&smobj->core.sem, timeout));
		while (ret && errno == EINTR);
	}

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

int semobj_inquire(struct semobj *smobj, size_t waitsz,
		   struct semobj_waitentry *waitlist,
		   int *val_r)
{
	struct cobalt_threadstat stat;
	struct cobalt_sem_info info;
	int nrwait, pidsz, n, ret;
	pid_t *pidlist = NULL;

	pidsz = sizeof(pid_t) * (waitsz / sizeof(*waitlist));
	if (pidsz > 0) {
		pidlist = pvmalloc(pidsz);
		if (pidlist == NULL)
			return -ENOMEM;
	}

	nrwait = cobalt_sem_inquire(&smobj->core.sem, &info, pidlist, pidsz);
	if (nrwait < 0)
		goto out;

	*val_r = info.value;

	if (pidlist == NULL)
		return nrwait;

	for (n = 0; n < nrwait; n++, waitlist++) {
		ret = cobalt_thread_stat(pidlist[n], &stat);
		/* If waiter disappeared, fill in a dummy entry. */
		if (ret) {
			waitlist->pid = -1;
			strcpy(waitlist->name, "???");
		} else {
			waitlist->pid = pidlist[n];
			strcpy(waitlist->name, stat.name);
		}
	}
out:
	if (pidlist)
		pvfree(pidlist);

	return nrwait;
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
	int sobj_flags = 0, ret;

	if (flags & SEMOBJ_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	/*
	 * We need a trampoline for finalizing a semobj, to escalate
	 * from a basic syncobj we receive to the semobj container.
	 */
	ret = syncobj_init(&smobj->core.sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libcopperplate, semobj_finalize));
	if (ret)
		return __bt(ret);

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

void semobj_uninit(struct semobj *smobj)
{
	syncobj_uninit(&smobj->core.sobj);
}

int semobj_post(struct semobj *smobj)
{
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&smobj->core.sobj, &syns);
	if (ret)
		return ret;

	if (++smobj->core.value <= 0)
		syncobj_grant_one(&smobj->core.sobj);
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
		syncobj_grant_all(&smobj->core.sobj);
	}

	syncobj_unlock(&smobj->core.sobj, &syns);

	return 0;
}

int semobj_wait(struct semobj *smobj, const struct timespec *timeout)
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

	ret = syncobj_wait_grant(&smobj->core.sobj, timeout, &syns);
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

int semobj_inquire(struct semobj *smobj, size_t waitsz,
		   struct semobj_waitentry *waitlist,
		   int *val_r)
{
	struct threadobj *thobj;
	struct syncstate syns;
	int ret, nrwait;

	ret = syncobj_lock(&smobj->core.sobj, &syns);
	if (ret)
		return ret;

	nrwait = syncobj_count_grant(&smobj->core.sobj);
	if (nrwait > 0) {
		syncobj_for_each_grant_waiter(&smobj->core.sobj, thobj) {
			waitlist->pid = threadobj_get_pid(thobj);
			strcpy(waitlist->name, threadobj_get_name(thobj));
			waitlist++;
		}
	}

	*val_r = smobj->core.value;

	syncobj_unlock(&smobj->core.sobj, &syns);

	return nrwait;
}

#endif /* CONFIG_XENO_MERCURY */
