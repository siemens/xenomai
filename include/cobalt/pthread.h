/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _XENO_POSIX_PTHREAD_H
#define _XENO_POSIX_PTHREAD_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include <nucleus/xenomai.h>

#ifdef __KERNEL__
#include <linux/types.h>
#include <sched.h>
#endif /* __KERNEL__ */

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#define PTHREAD_STACK_MIN   8192
#else /* __XENO_SIM__ */
#define PTHREAD_STACK_MIN   1024
#endif /* __XENO_SIM__ */

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_INHERIT_SCHED  0
#define PTHREAD_EXPLICIT_SCHED 1

#define PTHREAD_SCOPE_SYSTEM  0
#define PTHREAD_SCOPE_PROCESS 1

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    0

#define PTHREAD_PRIO_NONE    0
#define PTHREAD_PRIO_INHERIT 1
#define PTHREAD_PRIO_PROTECT 2

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1

#define PTHREAD_CANCEL_ENABLE  0
#define PTHREAD_CANCEL_DISABLE 1

#define PTHREAD_CANCEL_DEFERRED     2
#define PTHREAD_CANCEL_ASYNCHRONOUS 3

#define PTHREAD_CANCELED  ((void *)-2)

#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_KEYS_MAX 128

#define PTHREAD_ONCE_INIT { 0x86860808, 0 }

struct timespec;

struct cobalt_thread;

typedef struct cobalt_thread *pthread_t;

typedef struct cobalt_threadattr {

	unsigned magic;
	int detachstate;
	size_t stacksize;
	int inheritsched;
	int policy;

	/* Non portable */
	struct sched_param_ex schedparam_ex;
	char *name;
	int fp;
	xnarch_cpumask_t affinity;

} pthread_attr_t;

/* pthread_mutexattr_t and pthread_condattr_t fit on 32 bits, for compatibility
   with libc. */
struct cobalt_key;
typedef struct cobalt_key *pthread_key_t;

typedef struct cobalt_once {
	unsigned magic;
	int routine_called;
} pthread_once_t;

#ifdef __KERNEL__
/* The following definitions are copied from linuxthread pthreadtypes.h. */
struct _pthread_fastlock
{
  long int __status;
  int __spinlock;
};

typedef struct
{
  struct _pthread_fastlock __c_lock;
  long __c_waiting;
  char __padding[48 - sizeof (struct _pthread_fastlock)
		 - sizeof (long) - sizeof (long long)];
  long long __align;
} pthread_cond_t;

typedef struct
{
  int __m_reserved;
  int __m_count;
  long __m_owner;
  int __m_kind;
  struct _pthread_fastlock __m_lock;
} pthread_mutex_t;

#endif /* __KERNEL__ */

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include <sched.h>
#include_next <pthread.h>
#include <nucleus/thread.h>
#include <cobalt/wrappers.h>

struct timespec;

#endif /* __KERNEL__ || __XENO_SIM__ */

#ifndef PTHREAD_PRIO_NONE
#define PTHREAD_PRIO_NONE    0
#endif /* !PTHREAD_PRIO_NONE */
#ifndef PTHREAD_PRIO_INHERIT
#define PTHREAD_PRIO_INHERIT 1
#endif /* !PTHREAD_PRIO_INHERIT */
#ifndef PTHREAD_PRIO_PROTECT
#define PTHREAD_PRIO_PROTECT 2
#endif /* !PTHREAD_PRIO_PROTECT */

#define PTHREAD_WARNSW     XNTRAPSW
#define PTHREAD_LOCK_SCHED XNLOCK
#define PTHREAD_CONFORMING XNTHREAD_STATE_SPARE1

struct cobalt_mutexattr {
	unsigned magic: 24;
	unsigned type: 2;
	unsigned protocol: 2;
	unsigned pshared: 1;
};

struct cobalt_condattr {
	unsigned magic: 24;
	unsigned clock: 2;
	unsigned pshared: 1;
};

struct cobalt_cond;

struct cobalt_threadstat {
	unsigned long status;
	unsigned long long xtime;
	unsigned long msw;
	unsigned long csw;
	unsigned long xsc;
	unsigned long pf;
};

struct cobalt_monitor;
struct cobalt_monitor_data;

#define COBALT_MONITOR_SHARED     0x1
#define COBALT_MONITOR_WAITGRANT  0x0
#define COBALT_MONITOR_WAITDRAIN  0x1

struct cobalt_monitor_shadow {
	struct cobalt_monitor *monitor;
	union {
		struct cobalt_monitor_data *data;
		unsigned int data_offset;
	} u;
	int flags;
};

#if defined(__KERNEL__) || defined(__XENO_SIM__)
typedef struct cobalt_mutexattr pthread_mutexattr_t;

typedef struct cobalt_condattr pthread_condattr_t;

#ifdef __cplusplus
extern "C" {
#endif

int pthread_attr_init(pthread_attr_t *attr);

int pthread_attr_destroy(pthread_attr_t *attr);

int pthread_attr_getdetachstate(const pthread_attr_t *attr,
				int *detachstate);

int pthread_attr_setdetachstate(pthread_attr_t *attr,
				int detachstate);

int pthread_attr_getstackaddr(const pthread_attr_t *attr,
			      void **stackaddr);

int pthread_attr_setstackaddr(pthread_attr_t *attr,
			      void *stackaddr);

int pthread_attr_getstacksize(const pthread_attr_t *attr,
			      size_t *stacksize);

int pthread_attr_setstacksize(pthread_attr_t *attr,
			      size_t stacksize);

int pthread_attr_getinheritsched(const pthread_attr_t *attr,
				 int *inheritsched);

int pthread_attr_setinheritsched(pthread_attr_t *attr,
				 int inheritsched);

int pthread_attr_getschedpolicy(const pthread_attr_t *attr,
				int *policy);

int pthread_attr_setschedpolicy(pthread_attr_t *attr,
				int policy);

int pthread_attr_getschedparam(const pthread_attr_t *attr,
			       struct sched_param *par);

int pthread_attr_getschedparam_ex(const pthread_attr_t *attr,
				  struct sched_param_ex *par);

int pthread_attr_setschedparam(pthread_attr_t *attr,
			       const struct sched_param *par);

int pthread_attr_setschedparam_ex(pthread_attr_t *attr,
				  const struct sched_param_ex *par);

int pthread_attr_getscope(const pthread_attr_t *attr,
			  int *scope);

int pthread_attr_setscope(pthread_attr_t *attr,
			  int scope);

int pthread_attr_getname_np(const pthread_attr_t *attr,
			    const char **name);

int pthread_attr_setname_np(pthread_attr_t *attr,
			    const char *name);

int pthread_attr_getfp_np(const pthread_attr_t *attr,
			  int *use_fp);

int pthread_attr_setfp_np(pthread_attr_t *attr,
			  int use_fp);

int pthread_attr_getaffinity_np(const pthread_attr_t *attr,
				xnarch_cpumask_t *mask);

int pthread_attr_setaffinity_np(pthread_attr_t *attr,
				xnarch_cpumask_t mask);

int pthread_mutexattr_init(pthread_mutexattr_t *attr);

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
			      int *type);

int pthread_mutexattr_settype(pthread_mutexattr_t *attr,
			      int type);

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
				  int *proto);

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
				  int proto);

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr, int *pshared);

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared);

int pthread_mutex_init(pthread_mutex_t *mutex,
		       const pthread_mutexattr_t *attr);

int pthread_mutex_destroy(pthread_mutex_t *mutex);

int pthread_mutex_trylock(pthread_mutex_t *mutex);

int pthread_mutex_lock(pthread_mutex_t *mutex);

int pthread_mutex_timedlock(pthread_mutex_t *mutex,
			    const struct timespec *to);

int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_condattr_init(pthread_condattr_t *attr);

int pthread_condattr_destroy(pthread_condattr_t *attr);

int pthread_condattr_getclock(const pthread_condattr_t *attr,
			      clockid_t *clk_id);

int pthread_condattr_setclock(pthread_condattr_t *attr,
			      clockid_t clk_id);

int pthread_condattr_getpshared(const pthread_condattr_t *attr, int *pshared);

int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

typedef struct {
	pthread_attr_t std;
	struct {
		int sched_policy;
		struct sched_param_ex sched_param;
	} nonstd;
} pthread_attr_ex_t;

typedef struct cobalt_monitor_shadow cobalt_monitor_t;

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
COBALT_DECL(int, pthread_yield(void));

COBALT_DECL(int, pthread_mutexattr_init(pthread_mutexattr_t *attr));

COBALT_DECL(int, pthread_mutexattr_destroy(pthread_mutexattr_t *attr));

COBALT_DECL(int, pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
					   int *type));

COBALT_DECL(int, pthread_mutexattr_settype(pthread_mutexattr_t *attr,
					   int type));

#ifdef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
COBALT_DECL(int, pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					       int *proto));

COBALT_DECL(int, pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					       int proto));
#endif

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

#ifndef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
COBALT_DECL(int, pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					       int *proto));

COBALT_DECL(int, pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					       int proto));
#endif

#ifndef HAVE_PTHREAD_CONDATTR_SETCLOCK
COBALT_DECL(int, pthread_condattr_getclock(const pthread_condattr_t *attr,
					   clockid_t *clk_id));

COBALT_DECL(int, pthread_condattr_setclock(pthread_condattr_t *attr,
					   clockid_t clk_id));
#endif

int pthread_make_periodic_np(pthread_t thread,
			     clockid_t clk_id,
			     struct timespec *starttp,
			     struct timespec *periodtp);

int pthread_wait_np(unsigned long *overruns_r);

int pthread_set_mode_np(int clrmask, int setmask,
			int *mask_r);

int pthread_set_name_np(pthread_t thread,
			const char *name);

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
#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_SIM__ */

#endif /* _XENO_POSIX_PTHREAD_H */
