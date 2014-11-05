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
#include <trank/native/event.h>
#include <trank/native/pipe.h>
#include "../alchemy/alarm.h"

#ifdef DOXYGEN_CPP

/**
 * @ingroup trank
 * @{
 *
 * @fn int COMPAT__rt_task_create(RT_TASK *task, const char *name, int stksize, int prio, int mode)
 * @brief Create a real-time task (compatibility service).
 *
 * This service creates a task with access to the full set of Xenomai
 * real-time services.
 *
 * This service creates a task with access to the full set of Xenomai
 * real-time services. If @a prio is non-zero, the new task belongs to
 * Xenomai's real-time FIFO scheduling class, aka SCHED_FIFO. If @a
 * prio is zero, the task belongs to the regular SCHED_OTHER class.
 *
 * Creating tasks with zero priority is useful for running non
 * real-time processes which may invoke blocking real-time services,
 * such as pending on a semaphore, reading from a message queue or a
 * buffer, and so on.
 *
 * Once created, the task is left dormant until it is actually started
 * by rt_task_start().
 *
 * @param task The address of a task descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, a copy of this string is
 * used for indexing the created task into the object registry.
 *
 * @param stksize The size of the stack (in bytes) for the new
 * task. If zero is passed, a system-dependent default size will be
 * substituted.
 *
 * @param prio The base priority of the new task. This value must be
 * in the [0 .. 99] range, where 0 is the lowest effective priority. 
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask:
 *
 * - T_FPU allows the task to use the FPU whenever available on the
 * platform. This flag may be omitted, as it is automatically set when
 * a FPU is present on the platform, cleared otherwise.
 *
 * - T_SUSP causes the task to start in suspended mode. In such a
 * case, the thread will have to be explicitly resumed using the
 * rt_task_resume() service for its execution to actually begin.
 *
 * - T_CPU(cpuid) makes the new task affine to CPU # @b cpuid. CPU
 * identifiers range from 0 to 7 (inclusive).
 *
 * - T_JOINABLE allows another task to wait on the termination of the
 * new task. rt_task_join() shall be called for this task to clean up
 * any resources after its termination.
 *
 * Passing T_FPU|T_CPU(1) in the @a mode parameter thus creates a task
 * with FPU support enabled and which will be affine to CPU #1.
 *
 * - When running over the Cobalt core, T_WARNSW causes the SIGDEBUG
 * signal to be sent to the current task whenever it switches to the
 * secondary mode. This feature is useful to detect unwanted
 * migrations to the Linux domain. This flag has no effect over the
 * Mercury core.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if either @a prio, @a mode or @a stksize are
 * invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the task.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered task.
 *
 * @apitags{thread-unrestricted, switch-secondary}
 *
 * @sideeffect
 *
 *   - calling rt_task_create() causes SCHED_FIFO tasks to switch to
 * secondary mode.
 *
 *   - members of Xenomai's SCHED_FIFO class running in the primary
 * domain have utmost priority over all Linux activities in the
 * system, including Linux interrupt handlers.
 *
 * @note Tasks can be referred to from multiple processes which all
 * belong to the same Xenomai session.
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 */

int COMPAT__rt_task_create(RT_TASK *task, const char *name,
			   int stksize, int prio, int mode);

/**
 * @fn int COMPAT__rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
 * @brief Make a real-time task periodic (compatibility service).
 *
 * Make a task periodic by programing its first release point and its
 * period in the processor time line.  @a task should then call
 * rt_task_wait_period() to sleep until the next periodic release
 * point in the processor timeline is reached.
 *
 * @param task The task descriptor.  If @a task is NULL, the current
 * task is made periodic. @a task must belong the current process.
 *
 * @param idate The initial (absolute) date of the first release
 * point, expressed in clock ticks (see note).  If @a idate is equal
 * to TM_NOW, the current system date is used.  Otherwise, if @a task
 * is NULL or equal to @a rt_task_self(), the caller is delayed until
 * @a idate has elapsed.
 *
 * @param period The period of the task, expressed in clock ticks (see
 * note). Passing TM_INFINITE stops the task's periodic timer if
 * enabled, then returns successfully.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is NULL but the caller is not a
 * Xenomai task, or if @a task is non-NULL but not a valid task
 * descriptor.
 *
 * - -ETIMEDOUT is returned if @a idate is different from TM_INFINITE
 * and represents a date in the past.
 *
 * @apitags{thread-unrestricted, switch-primary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 *
 * @note Unlike the original Xenomai 2.x call, this emulation delays
 * the caller until @a idate has elapsed only if @a task is NULL or
 * equal to rt_task_self().
 *
 * @sideeffect Over Cobalt, -EINVAL is returned if @a period is
 * different from TM_INFINITE but shorter than the user scheduling
 * latency value for the target system, as displayed by
 * /proc/xenomai/latency.
 *
 * @note The @a idate and @a period values are interpreted as a
 * multiple of the Alchemy clock resolution (see
 * --alchemy-clock-resolution option, defaults to 1 nanosecond).
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 */

int COMPAT__rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period);

/**
 * @fn int COMPAT__rt_alarm_create(RT_ALARM *alarm, const char *name)
 * @brief Create an alarm object (compatibility service).
 *
 * This routine creates an object triggering an alarm routine at a
 * specified time in the future. Alarms can be periodic or oneshot,
 * depending on the reload interval value passed to rt_alarm_start().
 * A task can wait for timeouts using the rt_alarm_wait() service.
 *
 * @param alarm The address of an alarm descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * alarm. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created alarm into the object registry.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * local pool in order to create the alarm.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered alarm.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{thread-unrestricted, switch-secondary}
 *
 * @note Alarms are process-private objects and thus cannot be shared
 * by multiple processes, even if they belong to the same Xenomai
 * session.
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 */

int COMPAT__rt_alarm_create(RT_ALARM *alarm, const char *name);

/**
 * @fn int rt_alarm_wait(RT_ALARM *alarm)
 * @brief Wait for the next alarm shot (compatibility service).
 *
 * This service allows the current task to suspend execution until the
 * specified alarm triggers. The priority of the current task is
 * raised above all other tasks - except those also undergoing an
 * alarm wait.
 *
 * @return Zero is returned upon success, after the alarm timed
 * out. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid alarm descriptor.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before the request is satisfied.
 *
 * - -EIDRM is returned if @a alarm is deleted while the caller was
 * sleeping on it. In such a case, @a alarm is no more valid upon
 * return of this service.
 *
 * @apitags{xthread-only, switch-primary}
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 *
 */

int rt_alarm_wait(RT_ALARM *alarm);

/**
 * @fn int COMPAT__rt_event_create(RT_EVENT *event, const char *name, unsigned long ivalue, int mode)
 * @brief Create an event flag group.
 *
 * This call is the legacy form of the rt_event_create() service,
 * using a long event mask. The new form uses a regular integer to
 * hold the event mask instead.
 *
 * @param event The address of an event descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * event. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created event into the object registry.
 *
 * @param ivalue The initial value of the group's event mask.
 *
 * @param mode The event group creation mode. The following flags can
 * be OR'ed into this bitmask:
 *
 * - EV_FIFO makes tasks pend in FIFO order on the event flag group.
 *
 * - EV_PRIO makes tasks pend in priority order on the event flag group.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mode is invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the event flag group.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered event flag group.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{thread-unrestricted, switch-secondary}
 *
 * @note Event flag groups can be shared by multiple processes which
 * belong to the same Xenomai session.
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 */
int COMPAT__rt_event_create(RT_EVENT *event, const char *name,
			    unsigned long ivalue, int mode);

/**
 * @fn int COMPAT__rt_event_signal(RT_EVENT *event, unsigned long mask)
 * @brief Signal an event.
 *
 * This call is the legacy form of the rt_event_signal() service,
 * using a long event mask. The new form uses a regular integer to
 * hold the event mask instead.
 *
 * @param event The event descriptor.
 *
 * @param mask The set of events to be posted.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not an event flag group
 * descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 */
int COMPAT__rt_event_signal(RT_EVENT *event, unsigned long mask);

/**
 * @fn int COMPAT__rt_event_clear(RT_EVENT *event,unsigned long mask,unsigned long *mask_r)
 * @brief Clear event flags.
 *
 * This call is the legacy form of the rt_event_clear() service,
 * using a long event mask. The new form uses a regular integer to
 * hold the event mask instead.

 * @param event The event descriptor.
 *
 * @param mask The set of event flags to be cleared.
 *
 * @param mask_r If non-NULL, @a mask_r is the address of a memory
 * location which will receive the previous value of the event flag
 * group before the flags are cleared.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a valid event flag group
 * descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 *
 * @deprecated This is a compatibility service from the Transition
 * Kit.
 */
int COMPAT__rt_event_clear(RT_EVENT *event,
			   unsigned long mask, unsigned long *mask_r);

/**
 * @fn int COMPAT__rt_pipe_create(RT_PIPE *pipe, const char *name, int minor, size_t poolsize)
 * @brief Create a message pipe.
 *
 * This call is the legacy form of the rt_pipe_create() service, which
 * returns a zero status upon success. The new form returns the @a
 * minor number assigned to the connection instead, which is useful
 * when P_MINOR_AUTO is specified in the call (see the discussion
 * about the @a minor parameter).
 *
 * This service opens a bi-directional communication channel for
 * exchanging messages between Xenomai threads and regular Linux
 * threads. Pipes natively preserve message boundaries, but can also
 * be used in byte-oriented streaming mode from Xenomai to Linux.
 *
 * rt_pipe_create() always returns immediately, even if no thread has
 * opened the associated special device file yet. On the contrary, the
 * non real-time side could block upon attempt to open the special
 * device file until rt_pipe_create() is issued on the same pipe from
 * a Xenomai thread, unless O_NONBLOCK was given to the open(2) system
 * call.
 *
 * @param pipe The address of a pipe descriptor which can be later used
 * to identify uniquely the created object, upon success of this call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * pipe. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created pipe into the object registry.
 *
 * Named pipes are supported through the use of the registry. Passing
 * a valid @a name parameter when creating a message pipe causes a
 * symbolic link to be created from
 * /proc/xenomai/registry/rtipc/xddp/@a name to the associated special
 * device (i.e. /dev/rtp*), so that the specific @a minor information
 * does not need to be known from those processes for opening the
 * proper device file. In such a case, both sides of the pipe only
 * need to agree upon a symbolic name to refer to the same data path,
 * which is especially useful whenever the @a minor number is picked
 * up dynamically using an adaptive algorithm, such as passing
 * P_MINOR_AUTO as @a minor value.
 *
 * @param minor The minor number of the device associated with the
 * pipe.  Passing P_MINOR_AUTO causes the minor number to be
 * auto-allocated. In such a case, a symbolic link will be
 * automatically created from
 * /proc/xenomai/registry/rtipc/xddp/@a name to the allocated pipe
 * device entry. Valid minor numbers range from 0 to
 * CONFIG_XENO_OPT_PIPE_NRDEV-1.
 *
 * @param poolsize Specifies the size of a dedicated buffer pool for the
 * pipe. Passing 0 means that all message allocations for this pipe are
 * performed on the Cobalt core heap.
 *
 * @return This compatibility call returns zero upon
 * success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the pipe.
 *
 * - -ENODEV is returned if @a minor is different from P_MINOR_AUTO
 * and is not a valid minor number.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered pipe.
 *
 * - -EBUSY is returned if @a minor is already open.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
int COMPAT__rt_pipe_create(RT_PIPE *pipe,
			   const char *name, int minor, size_t poolsize);
#else /* !DOXYGEN_CPP */

int rt_task_create(RT_TASK *task, const char *name,
		   int stksize, int prio, int mode)
{
	int ret, susp, cpus, cpu;
	cpu_set_t cpuset;

	susp = mode & T_SUSP;
	cpus = mode & T_CPUMASK;
	ret = __CURRENT(rt_task_create(task, name, stksize, prio,
				       mode & ~(T_SUSP|T_CPUMASK|T_LOCK)));
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
		/* Working on self, so -EIDRM can't happen. */
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

int rt_event_create(RT_EVENT *event, const char *name,
		    unsigned long ivalue, int mode)
{
	return __CURRENT(rt_event_create(event, name, ivalue, mode));
}

int rt_event_signal(RT_EVENT *event, unsigned long mask)
{
	return __CURRENT(rt_event_signal(event, mask));
}

int rt_event_clear(RT_EVENT *event, unsigned long mask,
		   unsigned long *mask_r)
{
	unsigned int _mask;
	int ret;

	ret = __CURRENT(rt_event_clear(event, mask, &_mask));
	if (ret)
		return ret;

	*mask_r = _mask;

	return 0;
}

int rt_pipe_create(RT_PIPE *pipe, const char *name,
		   int minor, size_t poolsize)
{
	int ret;

	ret = __CURRENT(rt_pipe_create(pipe, name, minor, poolsize));

	return ret < 0 ? ret : 0;
}

#endif	/* !DOXYGEN_CPP */

/** @} */
