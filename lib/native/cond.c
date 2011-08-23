/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#include <native/syscall.h>
#include <native/mutex.h>
#include <native/cond.h>

extern int __native_muxid;

int rt_cond_create(RT_COND *cond, const char *name)
{
	return XENOMAI_SKINCALL2(__native_muxid, __native_cond_create, cond,
				 name);
}

int rt_cond_bind(RT_COND *cond, const char *name, RTIME timeout)
{
	return XENOMAI_SKINCALL3(__native_muxid,
				 __native_cond_bind, cond, name, &timeout);
}

int rt_cond_delete(RT_COND *cond)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_cond_delete, cond);
}

struct rt_cond_cleanup_t {
	RT_MUTEX *mutex;
	unsigned saved_lockcnt;
	int err;
};

static void __rt_cond_cleanup(void *data)
{
	struct rt_cond_cleanup_t *c = (struct rt_cond_cleanup_t *)data;
	int err;

	do {
		err = XENOMAI_SKINCALL2(__native_muxid,
					__native_cond_wait_epilogue, c->mutex,
					c->saved_lockcnt);
	} while (err == EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
	c->mutex->lockcnt = c->saved_lockcnt;
#endif /* CONFIG_XENO_FASTSYNCH */
}

int rt_cond_wait(RT_COND *cond, RT_MUTEX *mutex, RTIME timeout)
{
	struct rt_cond_cleanup_t c = {
		.mutex = mutex,
	};
	int err, oldtype;

	pthread_cleanup_push(&__rt_cond_cleanup, &c);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

#ifdef CONFIG_XENO_FASTSYNCH
	c.saved_lockcnt = mutex->lockcnt;
#endif /* CONFIG_XENO_FASTSYNCH */

	err = XENOMAI_SKINCALL5(__native_muxid,
				__native_cond_wait_prologue, cond, mutex,
				&c.saved_lockcnt, XN_RELATIVE, &timeout);

	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__native_muxid,
					__native_cond_wait_epilogue, mutex,
					c.saved_lockcnt);

#ifdef CONFIG_XENO_FASTSYNCH
	mutex->lockcnt = c.saved_lockcnt;
#endif /* CONFIG_XENO_FASTSYNCH */

	pthread_testcancel();

	return err ?: c.err;
}

int rt_cond_wait_until(RT_COND *cond, RT_MUTEX *mutex, RTIME timeout)
{
	struct rt_cond_cleanup_t c = {
		.mutex = mutex,
	};
	int err, oldtype;

	pthread_cleanup_push(&__rt_cond_cleanup, &c);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

#ifdef CONFIG_XENO_FASTSYNCH
	c.saved_lockcnt = mutex->lockcnt;
#endif /* CONFIG_XENO_FASTSYNCH */

	err = XENOMAI_SKINCALL5(__native_muxid,
				__native_cond_wait_prologue, cond, mutex,
				&c.saved_lockcnt, XN_REALTIME, &timeout);

	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__native_muxid,
					__native_cond_wait_epilogue, mutex,
					c.saved_lockcnt);

#ifdef CONFIG_XENO_FASTSYNCH
	mutex->lockcnt = c.saved_lockcnt;
#endif /* CONFIG_XENO_FASTSYNCH */

	pthread_testcancel();

	return err ?: c.err;
}

int rt_cond_signal(RT_COND *cond)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_cond_signal, cond);
}

int rt_cond_broadcast(RT_COND *cond)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_cond_broadcast, cond);
}

int rt_cond_inquire(RT_COND *cond, RT_COND_INFO *info)
{
	return XENOMAI_SKINCALL2(__native_muxid, __native_cond_inquire, cond,
				 info);
}
