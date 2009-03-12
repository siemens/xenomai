/*
 * Copyright (C) 2005 Heikki Lindholm <holindho@cs.helsinki.fi>.
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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

#include <pthread.h>
#include <semaphore.h>

/*
 * This file maintains a list of placeholders for routines that we do
 * NOT want to be wrapped to their Xenomai POSIX API counterparts when
 * used internally by the VRTX interface.
 */

__attribute__ ((weak))
int __real_pthread_setschedparam(pthread_t thread,
				 int policy, const struct sched_param *param)
{
	return pthread_setschedparam(thread, policy, param);
}

__attribute__ ((weak))
int __real_pthread_create(pthread_t *tid,
			  const pthread_attr_t * attr,
			  void *(*start) (void *), void *arg)
{
	return pthread_create(tid, attr, start, arg);
}

__attribute__ ((weak))
int __real_sem_init(sem_t *sem, int pshared, unsigned value)
{
	return sem_init(sem, pshared, value);
}

__attribute__ ((weak))
int __real_sem_destroy(sem_t *sem)
{
	return sem_destroy(sem);
}

__attribute__ ((weak))
int __real_sem_post(sem_t *sem)
{
	return sem_post(sem);
}

__attribute__ ((weak))
int __real_sem_wait(sem_t *sem)
{
	return sem_wait(sem);
}
