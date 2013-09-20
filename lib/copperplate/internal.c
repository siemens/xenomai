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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <linux/unistd.h>
#include <copperplate/clockobj.h>
#include <copperplate/threadobj.h>
#include <copperplate/init.h>
#include "internal.h"

pid_t copperplate_get_tid(void)
{
	/*
	 * XXX: The nucleus maintains a hash table indexed on
	 * task_pid_vnr() values for mapped shadows. This is what
	 * __NR_gettid retrieves as well in Cobalt mode.
	 */
	return syscall(__NR_gettid);
}

#ifdef CONFIG_XENO_COBALT

int copperplate_probe_node(unsigned int id)
{
	/*
	 * XXX: this call does NOT migrate to secondary mode therefore
	 * may be used in time-critical contexts. However, since the
	 * nucleus has to know about a probed thread to find out
	 * whether it exists, copperplate_init() must always be
	 * invoked from a real-time shadow, so that __node_id can be
	 * matched.
	 */
	return pthread_probe_np((pid_t)id) == 0;
}

int copperplate_create_thread(const struct corethread_attributes *cta,
			      pthread_t *tid)
{
	struct sched_param_ex param_ex;
	pthread_attr_ex_t attr_ex;
	size_t stacksize;
	int policy, ret;

	stacksize = cta->stacksize;
	if (stacksize < PTHREAD_STACK_MIN * 4)
		stacksize = PTHREAD_STACK_MIN * 4;

	param_ex.sched_priority = cta->prio;
	policy = cta->prio ? SCHED_RT : SCHED_OTHER;
	pthread_attr_init_ex(&attr_ex);
	pthread_attr_setinheritsched_ex(&attr_ex, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy_ex(&attr_ex, policy);
	pthread_attr_setschedparam_ex(&attr_ex, &param_ex);
	pthread_attr_setstacksize_ex(&attr_ex, stacksize);
	pthread_attr_setdetachstate_ex(&attr_ex, cta->detachstate);
	ret = __bt(-pthread_create_ex(tid, &attr_ex, cta->start, cta->arg));
	pthread_attr_destroy_ex(&attr_ex);

	return ret;
}

int copperplate_renice_thread(pthread_t tid, int prio)
{
	struct sched_param_ex param_ex;
	int policy;

	param_ex.sched_priority = prio;
	policy = prio ? SCHED_RT : SCHED_OTHER;

	return __bt(-pthread_setschedparam_ex(tid, policy, &param_ex));
}

#else /* CONFIG_XENO_MERCURY */

int copperplate_probe_node(unsigned int id)
{
	return kill((pid_t)id, 0) == 0;
}

int copperplate_create_thread(const struct corethread_attributes *cta,
			      pthread_t *tid)
{
	struct sched_param param;
	pthread_attr_t attr;
	size_t stacksize;
	int policy, ret;

	stacksize = cta->stacksize;
	if (stacksize < PTHREAD_STACK_MIN * 4)
		stacksize = PTHREAD_STACK_MIN * 4;

	param.sched_priority = cta->prio;
	policy = cta->prio ? SCHED_RT : SCHED_OTHER;
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, policy);
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, stacksize);
	pthread_attr_setdetachstate(&attr, cta->detachstate);
	ret = __bt(-pthread_create(tid, &attr, cta->start, cta->arg));
	pthread_attr_destroy(&attr);

	return ret;
}

int copperplate_renice_thread(pthread_t tid, int prio)
{
	struct sched_param param;
	int policy;

	param.sched_priority = prio;
	policy = prio ? SCHED_RT : SCHED_OTHER;

	return __bt(-__RT(pthread_setschedparam(tid, policy, &param)));
}

#endif  /* CONFIG_XENO_MERCURY */

void panic(const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	__panic(thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
	va_end(ap);
}

void warning(const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	__warning(thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
	va_end(ap);
}
