/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include <trank/native/task.h>
#include <trank/native/alarm.h>
#include "../alchemy/alarm.h"

int rt_task_create(RT_TASK *task, const char *name,
		   int stksize, int prio, int mode)
{
	int ret, susp, cpus, cpu;
	cpu_set_t cpuset;

	susp = mode & T_SUSP;
	cpus = mode & T_CPUMASK;
	ret = __CURRENT(rt_task_create(task, name, stksize, prio,
				       mode & ~(T_SUSP|T_CPUMASK)));
	if (ret)
		return ret;

	if (cpus) {
		CPU_ZERO(&cpuset);
		for (cpu = 0, cpus >>= 24;
		     cpus && cpu < 8; cpu++, cpus >>= 1) {
			if (cpus & 1)
				CPU_SET(cpu, &cpuset);
		}
		ret = rt_task_set_affinity(task, &cpuset);
		if (ret) {
			rt_task_delete(task);
			return ret;
		}
	}

	return susp ? rt_task_suspend(task) : 0;
}

int rt_task_spawn(RT_TASK *task, const char *name,
		  int stksize, int prio, int mode,
		  void (*entry)(void *arg), void *arg)
{
	int ret;

	ret = rt_task_create(task, name, stksize, prio, mode);
	if (ret)
		return ret;

	return rt_task_start(task, entry, arg);
}

int rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
{
	int ret;

	ret = __CURRENT(rt_task_set_periodic(task, idate, period));
	if (ret)
		return ret;

	if (idate != TM_NOW) {
		if (task == NULL || task == rt_task_self())
			ret = rt_task_wait_period(NULL);
		else
			trank_warning("task won't wait for start time");
	}

	return ret;
}

struct trank_alarm_wait {
	pthread_mutex_t lock;
	pthread_cond_t event;
	int alarm_pulses;
};

static void trank_alarm_handler(void *arg)
{
	struct trank_alarm_wait *aw = arg;

	__RT(pthread_mutex_lock(&aw->lock));
	aw->alarm_pulses++;
	__RT(pthread_cond_broadcast(&aw->event));
	__RT(pthread_mutex_unlock(&aw->lock));
}

int rt_alarm_create(RT_ALARM *alarm, const char *name)
{
	struct trank_alarm_wait *aw;
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	int ret;

	aw = xnmalloc(sizeof(*aw));
	if (aw == NULL)
		return -ENOMEM;

	aw->alarm_pulses = 0;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&aw->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		goto fail_lock;

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_PRIVATE);
	pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE);
	ret = __bt(-pthread_cond_init(&aw->event, &cattr));
	pthread_condattr_destroy(&cattr);
	if (ret)
		goto fail_cond;

	ret = __CURRENT(rt_alarm_create(alarm, name, trank_alarm_handler, aw));
	if (ret)
		goto fail_alarm;

	return 0;
fail_alarm:
	__RT(pthread_cond_destroy(&aw->event));
fail_cond:
	__RT(pthread_mutex_destroy(&aw->lock));
fail_lock:
	xnfree(aw);

	return ret;
}

static struct alchemy_alarm *find_alarm(RT_ALARM *alarm)
{
	struct alchemy_alarm *acb;

	if (bad_pointer(alarm))
		return NULL;

	acb = (struct alchemy_alarm *)alarm->handle;
	if (bad_pointer(acb) || acb->magic != alarm_magic)
		return NULL;

	return acb;
}

int rt_alarm_wait(RT_ALARM *alarm)
{
	struct threadobj *current = threadobj_current();
	struct sched_param_ex param_ex;
	struct trank_alarm_wait *aw;
	struct alchemy_alarm *acb;
	int ret, prio, pulses;

	acb = find_alarm(alarm);
	if (acb == NULL)
		return -EINVAL;

	threadobj_lock(current);
	prio = threadobj_get_priority(current);
	if (prio != threadobj_irq_prio) {
		param_ex.sched_priority = threadobj_irq_prio;
		threadobj_set_schedparam(current, SCHED_FIFO, &param_ex);
	}
	threadobj_unlock(current);

	aw = acb->arg;

	/*
	 * Emulate the original behavior: wait for the next pulse (no
	 * event buffering, broadcast to all waiters), while
	 * preventing spurious wakeups.
	 */
	__RT(pthread_mutex_lock(&aw->lock));

	pulses = aw->alarm_pulses;

	for (;;) {
		ret = -__RT(pthread_cond_wait(&aw->event, &aw->lock));
		if (ret || aw->alarm_pulses != pulses)
			break;
	}

	__RT(pthread_mutex_unlock(&aw->lock));

	return __bt(ret);
}

int rt_alarm_delete(RT_ALARM *alarm)
{
	struct trank_alarm_wait *aw;
	struct alchemy_alarm *acb;
	int ret;

	acb = find_alarm(alarm);
	if (acb == NULL)
		return -EINVAL;

	aw = acb->arg;
	ret = __CURRENT(rt_alarm_delete(alarm));
	if (ret)
		return ret;

	__RT(pthread_cond_destroy(&aw->event));
	__RT(pthread_mutex_destroy(&aw->lock));
	xnfree(aw);

	return 0;
}
