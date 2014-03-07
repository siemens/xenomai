/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#pragma GCC system_header
#include_next <pthread.h>

#ifndef _COBALT_PTHREAD_H
#define _COBALT_PTHREAD_H

#include <cobalt/wrappers.h>
#include <cobalt/uapi/thread.h>

typedef struct pthread_attr_ex {
	pthread_attr_t std;
	struct {
		int personality;
		int sched_policy;
		struct sched_param_ex sched_param;
	} nonstd;
} pthread_attr_ex_t;

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, pthread_attr_setschedpolicy(pthread_attr_t *attr,
					     int policy));

COBALT_DECL(int, pthread_attr_setschedparam(pthread_attr_t *attr,
					    const struct sched_param *par));

COBALT_DECL(int, pthread_create(pthread_t *tid,
				const pthread_attr_t *attr,
				void *(*start) (void *),
				void *arg));

COBALT_DECL(int, pthread_detach(pthread_t thread));

COBALT_DECL(int, pthread_getschedparam(pthread_t thread,
				       int *policy,
				       struct sched_param *param));

COBALT_DECL(int, pthread_setschedparam(pthread_t thread,
				       int policy,
				       const struct sched_param *param));

#ifndef pthread_yield	/* likely uClibc wrapping otherwise. */
COBALT_DECL(int, pthread_yield(void));
#endif

COBALT_DECL(int, pthread_mutexattr_init(pthread_mutexattr_t *attr));

COBALT_DECL(int, pthread_mutexattr_destroy(pthread_mutexattr_t *attr));

COBALT_DECL(int, pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
					   int *type));

COBALT_DECL(int, pthread_mutexattr_settype(pthread_mutexattr_t *attr,
					   int type));

COBALT_DECL(int, pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
					      int *pshared));

COBALT_DECL(int, pthread_mutexattr_setpshared(pthread_mutexattr_t *attr,
					      int pshared));

COBALT_DECL(int, pthread_mutex_init(pthread_mutex_t *mutex,
				    const pthread_mutexattr_t *attr));

COBALT_DECL(int, pthread_mutex_destroy(pthread_mutex_t *mutex));

COBALT_DECL(int, pthread_mutex_lock(pthread_mutex_t *mutex));

COBALT_DECL(int, pthread_mutex_timedlock(pthread_mutex_t *mutex,
					 const struct timespec *to));

COBALT_DECL(int, pthread_mutex_trylock(pthread_mutex_t *mutex));

COBALT_DECL(int, pthread_mutex_unlock(pthread_mutex_t *mutex));

COBALT_DECL(int, pthread_condattr_init(pthread_condattr_t *attr));

COBALT_DECL(int, pthread_condattr_destroy(pthread_condattr_t *attr));

COBALT_DECL(int, pthread_condattr_getclock(const pthread_condattr_t *attr,
					   clockid_t *clk_id));

COBALT_DECL(int, pthread_condattr_setclock(pthread_condattr_t *attr,
					   clockid_t clk_id));

COBALT_DECL(int, pthread_condattr_getpshared(const pthread_condattr_t *attr,
					     int *pshared));

COBALT_DECL(int, pthread_condattr_setpshared(pthread_condattr_t *attr,
					     int pshared));

COBALT_DECL(int, pthread_cond_init (pthread_cond_t *cond,
				    const pthread_condattr_t *attr));

COBALT_DECL(int, pthread_cond_destroy(pthread_cond_t *cond));

COBALT_DECL(int, pthread_cond_wait(pthread_cond_t *cond,
				   pthread_mutex_t *mutex));

COBALT_DECL(int, pthread_cond_timedwait(pthread_cond_t *cond,
					pthread_mutex_t *mutex,
					const struct timespec *abstime));

COBALT_DECL(int, pthread_cond_signal(pthread_cond_t *cond));

COBALT_DECL(int, pthread_cond_broadcast(pthread_cond_t *cond));

COBALT_DECL(int, pthread_kill(pthread_t tid, int sig));

COBALT_DECL(int, pthread_join(pthread_t tid, void **retval));

COBALT_DECL(int, pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					       int *proto));

COBALT_DECL(int, pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					       int proto));

COBALT_DECL(int, pthread_condattr_getclock(const pthread_condattr_t *attr,
					   clockid_t *clk_id));

COBALT_DECL(int, pthread_condattr_setclock(pthread_condattr_t *attr,
					   clockid_t clk_id));

int pthread_make_periodic_np(pthread_t thread,
			     clockid_t clk_id,
			     struct timespec *starttp,
			     struct timespec *periodtp);

int pthread_wait_np(unsigned long *overruns_r);

int pthread_set_mode_np(int clrmask, int setmask,
			int *mask_r);

int pthread_set_name_np(pthread_t thread,
			const char *name);

COBALT_DECL(int, pthread_setname_np(pthread_t thread, const char *name));

int pthread_probe_np(pid_t tid);

int pthread_create_ex(pthread_t *tid,
		      const pthread_attr_ex_t *attr_ex,
		      void *(*start)(void *),
		      void *arg);

int pthread_getschedparam_ex(pthread_t tid,
			     int *pol,
			     struct sched_param_ex *par);

int pthread_setschedparam_ex(pthread_t tid,
			     int pol,
			     const struct sched_param_ex *par);

int pthread_attr_init_ex(pthread_attr_ex_t *attr_ex);

int pthread_attr_destroy_ex(pthread_attr_ex_t *attr_ex);

int pthread_attr_setschedpolicy_ex(pthread_attr_ex_t *attr_ex,
				   int policy);

int pthread_attr_getschedpolicy_ex(const pthread_attr_ex_t *attr_ex,
				   int *policy);

int pthread_attr_setschedparam_ex(pthread_attr_ex_t *attr_ex,
				  const struct sched_param_ex *param_ex);

int pthread_attr_getschedparam_ex(const pthread_attr_ex_t *attr_ex,
				  struct sched_param_ex *param_ex);

int pthread_attr_getinheritsched_ex(const pthread_attr_ex_t *attr_ex,
				    int *inheritsched);

int pthread_attr_setinheritsched_ex(pthread_attr_ex_t *attr_ex,
				    int inheritsched);

int pthread_attr_getdetachstate_ex(const pthread_attr_ex_t *attr_ex,
				   int *detachstate);

int pthread_attr_setdetachstate_ex(pthread_attr_ex_t *attr_ex,
				   int detachstate);

int pthread_attr_setdetachstate_ex(pthread_attr_ex_t *attr_ex,
				   int detachstate);

int pthread_attr_getstacksize_ex(const pthread_attr_ex_t *attr_ex,
				 size_t *stacksize);

int pthread_attr_setstacksize_ex(pthread_attr_ex_t *attr_ex,
				 size_t stacksize);

int pthread_attr_getscope_ex(const pthread_attr_ex_t *attr_ex,
			     int *scope);

int pthread_attr_setscope_ex(pthread_attr_ex_t *attr_ex,
			     int scope);

int pthread_attr_getpersonality_ex(const pthread_attr_ex_t *attr_ex,
				   int *personality);

int pthread_attr_setpersonality_ex(pthread_attr_ex_t *attr_ex,
				   int personality);

#ifdef __UCLIBC__

#include <errno.h>

/*
 * Mutex PI and priority ceiling settings may not be available with
 * uClibc. Define the protocol values in the same terms as the
 * standard enum found in glibc to allow application code to enable
 * them.
 */
enum {
	PTHREAD_PRIO_NONE,
	PTHREAD_PRIO_INHERIT,
	PTHREAD_PRIO_PROTECT
};

/*
 * uClibc does not provide the following routines, so we define them
 * here. Note: let the compiler decides whether it wants to actually
 * inline these routines, i.e. do not force always_inline.
 */
inline __attribute__ ((weak))
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
		   void (*child)(void))
{
	return 0;
}

inline __attribute__ ((weak))
int pthread_getattr_np(pthread_t th, pthread_attr_t *attr)
{
	return ENOSYS;
}

#endif /* __UCLIBC__ */

#ifdef __cplusplus
}
#endif

#endif /* !_COBALT_PTHREAD_H */
