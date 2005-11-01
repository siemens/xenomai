/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#ifndef _XENO_SKIN_POSIX_H
#define _XENO_SKIN_POSIX_H

#define PSE51_SKIN_VERSION_STRING  "1.0"
#define PSE51_SKIN_VERSION_CODE    0x00010000
#define PSE51_SKIN_MAGIC           0x50534531

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/version.h>
#include <linux/signal.h>
#else /* !__KERNEL__ */
/* For INT_MAX in user-space, kernel space finds this in
   linux/kernel.h */
#include <limits.h>
#include <fcntl.h>              /* For O_RDONLY, etc... */
#include <time.h>               /* For struct itimerspec */
/* Include libc headers so that we are unable to redefine their contents, and
   avoid any side effect if they were included by users. */
#include <signal.h>
typedef unsigned mqd_t;
#endif /* !__KERNEL__ */

#include <xenomai/nucleus/xenomai.h>

#ifndef BEGIN_C_DECLS
#if __cplusplus
#define BEGIN_C_DECLS extern "C" {
#define END_C_DECLS   }
#else
#define BEGIN_C_DECLS
#define END_C_DECLS
#endif
#endif

#undef PTHREAD_STACK_MIN
#undef PTHREAD_DESTRUCTOR_ITERATIONS
#undef PTHREAD_KEYS_MAX
#undef SCHED_FIFO
#undef SCHED_RR
#undef SCHED_OTHER
#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember
#undef sigaction
#undef sigqueue
#undef SIGRTMIN
#undef SIGRTMAX
#undef TIMER_ABSTIME

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#endif /* __XENO_SIM__ */

/* Error Codes (pse51_errno_location is implemented in sem.c). */
/* errno values pasted from Linux asm/errno.h and bits/errno.h (ENOTSUP). */
#define ENOTSUP         EOPNOTSUPP
#define	ETIMEDOUT	110	/* Connection timed out */

#define errno (*pse51_errno_location())

BEGIN_C_DECLS
int *pse51_errno_location(void);
END_C_DECLS

/* Threads attributes. */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_EXPLICIT_SCHED 0
#define PTHREAD_INHERIT_SCHED  1

#define PTHREAD_SCOPE_SYSTEM  0
#define PTHREAD_SCOPE_PROCESS 1

#define SCHED_FIFO  1
#define SCHED_RR    2
#define SCHED_OTHER 3

#define PTHREAD_STACK_MIN   1024

typedef struct pse51_threadattr {

    unsigned magic;
    int detachstate;
    size_t stacksize;
    int inheritsched;
    int policy;
    struct sched_param schedparam;

    /* Non portable */
    char *name;
    int fp;
    xnarch_cpumask_t affinity;

} pthread_attr_t;


BEGIN_C_DECLS

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

int pthread_attr_setschedparam(pthread_attr_t *attr,
			       const struct sched_param *par);

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

int pthread_attr_getaffinity_np (const pthread_attr_t *attr,
                                 xnarch_cpumask_t *mask);

int pthread_attr_setaffinity_np (pthread_attr_t *attr,
                                 xnarch_cpumask_t mask);

END_C_DECLS

/* Threads. */
struct pse51_thread;

typedef struct pse51_thread *pthread_t;


BEGIN_C_DECLS

int pthread_create(pthread_t *tid,
		   const pthread_attr_t *attr,
		   void *(*start) (void *),
		   void *arg );

int pthread_detach(pthread_t thread);

int pthread_equal(pthread_t t1,
		  pthread_t t2);

void pthread_exit(void *value_ptr);

int pthread_join(pthread_t thread,
		 void **value_ptr);

pthread_t pthread_self(void);

int sched_yield(void);

END_C_DECLS

/* Scheduler interface. */

BEGIN_C_DECLS

int sched_get_priority_min(int policy);

int sched_get_priority_max(int policy);

int sched_rr_get_interval(int pid, struct timespec *interval);

int pthread_getschedparam(pthread_t tid,
			  int *pol,
			  struct sched_param *par);

int pthread_setschedparam(pthread_t tid,
			  int pol,
			  const struct sched_param *par);

END_C_DECLS

/* Mutex attributes. */

#define PTHREAD_MUTEX_DEFAULT    0
#define PTHREAD_MUTEX_NORMAL     1
#define PTHREAD_MUTEX_RECURSIVE  2
#define PTHREAD_MUTEX_ERRORCHECK 3

#define PTHREAD_PRIO_NONE    0
#define PTHREAD_PRIO_INHERIT 1
#define PTHREAD_PRIO_PROTECT 2

typedef struct pse51_mutexattr {
    unsigned magic;
    int type;
    int protocol;
} pthread_mutexattr_t;

BEGIN_C_DECLS

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

END_C_DECLS

/* Mutex. */

typedef struct pse51_mutex {
    unsigned magic;
    xnsynch_t synchbase;
    xnholder_t link;            /* Link in pse51_mutexq */
    pthread_mutexattr_t attr;
    pthread_t owner;
    unsigned count;             /* lock count. */
    unsigned condvars;          /* count of condition variables using this
				   mutex. */
} pthread_mutex_t;

BEGIN_C_DECLS

int pthread_mutex_init(pthread_mutex_t *mutex,
		       const pthread_mutexattr_t *attr);

int pthread_mutex_destroy(pthread_mutex_t *mutex);

int pthread_mutex_trylock(pthread_mutex_t *mutex);

int pthread_mutex_lock(pthread_mutex_t *mutex);

int pthread_mutex_timedlock(pthread_mutex_t *mutex,
			    const struct timespec *to);

int pthread_mutex_unlock(pthread_mutex_t *mutex);

END_C_DECLS

/* Condition variables attributes */
#ifndef __KERNEL__
#undef CLOCK_MONOTONIC
#undef CLOCK_REALTIME
typedef enum pse51_clockid {
    CLOCK_REALTIME  =0,		/* For absolute timeouts. */
    CLOCK_MONOTONIC =1		/* For relative timeouts. */
} clockid_t;
#endif /* __KERNEL__ */

typedef struct pse51_condattr {
    unsigned magic;
    clockid_t clock;
} pthread_condattr_t;

BEGIN_C_DECLS

int pthread_condattr_init(pthread_condattr_t *attr);

int pthread_condattr_destroy(pthread_condattr_t *attr);

int pthread_condattr_getclock(const pthread_condattr_t *attr,
			      clockid_t *clk_id);

int pthread_condattr_setclock(pthread_condattr_t *attr,
			      clockid_t clk_id);

END_C_DECLS

/* Condition Variables */
typedef struct pse51_cond {
    unsigned magic;
    xnsynch_t synchbase;
    xnholder_t link;            /* Link in pse51_condq */
    pthread_condattr_t attr;
    struct pse51_mutex *mutex;
} pthread_cond_t;

BEGIN_C_DECLS

int pthread_cond_init(pthread_cond_t *cond,
		      const pthread_condattr_t *attr);

int pthread_cond_destroy(pthread_cond_t *cond);

int pthread_cond_wait(pthread_cond_t *cond,
		      pthread_mutex_t *mutex);

int pthread_cond_timedwait(pthread_cond_t *cond,
			   pthread_mutex_t *mutex, 
                           const struct timespec *abstime);

int pthread_cond_signal(pthread_cond_t *cond);

int pthread_cond_broadcast(pthread_cond_t *cond);

END_C_DECLS

/* Semaphores */
#define SEM_VALUE_MAX (INT_MAX)
#define SEM_FAILED    NULL

typedef struct pse51_sem {
    unsigned magic;
    xnholder_t link;            /* Link in pse51_semq */
    xnsynch_t synchbase;
    int value;
} sem_t;

BEGIN_C_DECLS

int sem_init(sem_t *sem,
	     int pshared,
	     unsigned int value);

int sem_destroy(sem_t *sem);

int sem_post(sem_t *sem);

int sem_trywait(sem_t *sem);

int sem_wait(sem_t *sem);

int sem_timedwait(sem_t *sem,
		  const struct timespec *abs_timeout);

int sem_getvalue(sem_t *sem,
		 int *value);

sem_t *sem_open(const char *name, int oflag, ...);

int sem_close(sem_t *sem);

int sem_unlink(const char *name);

END_C_DECLS

/* Cancellation. */
#define PTHREAD_CANCEL_ENABLE  0
#define PTHREAD_CANCEL_DISABLE 1

#define PTHREAD_CANCEL_DEFERRED     2
#define PTHREAD_CANCEL_ASYNCHRONOUS 3

#define PTHREAD_CANCELED  ((void *)-2)

BEGIN_C_DECLS

int pthread_cancel(pthread_t thread);

void pthread_cleanup_push(void (*routine)(void *),
			  void *arg);

void pthread_cleanup_pop(int execute);

int pthread_setcancelstate(int state,
			   int *oldstate);

int pthread_setcanceltype(int type,
			  int *oldtype);

void pthread_testcancel(void);

END_C_DECLS

/* Signals. */

#ifdef __KERNEL__
/* These are not defined in kernel-space headers. */
#define sa_sigaction sa_handler
typedef void (*sighandler_t) (int sig);
typedef unsigned long sig_atomic_t;
#define DELAYTIMER_MAX UINT_MAX
#endif /* ! __KERNEL__ */

#define sigaction(sig, action, old) pse51_sigaction(sig, action, old)
#define sigemptyset pse51_sigemptyset
#define sigfillset pse51_sigfillset
#define sigaddset pse51_sigaddset
#define sigdelset pse51_sigdelset
#define sigismember pse51_sigismember
#define sigqueue pse51_sigqueue

#define SIGRTMIN 33
#define SIGRTMAX 64

BEGIN_C_DECLS

int sigemptyset(sigset_t *set);

int sigfillset(sigset_t *set);

int sigaddset(sigset_t *set,
	      int signum);

int sigdelset(sigset_t *set,
	      int signum);

int sigismember(const sigset_t *set,
		int signum);

int pthread_kill(pthread_t thread,
		 int sig);

int pthread_sigmask(int how,
		    const sigset_t *set,
		    sigset_t *oset);

int sigaction(int sig,
	      const struct sigaction *action,
	      struct sigaction *old);

int sigpending(sigset_t *set);

int sigwait(const sigset_t *set,
	    int *sig);

/* Real-time signals. */
int sigwaitinfo(const sigset_t *__restrict__ set,
                siginfo_t *__restrict__ info);

int sigtimedwait(const sigset_t *__restrict__ user_set,
                 siginfo_t *__restrict__ info,
                 const struct timespec *__restrict__ timeout);

/* Depart from POSIX here, we use a thread id instead of a process id. */
int sigqueue (pthread_t thread, int sig, union sigval value);

END_C_DECLS

/* Thread-specific data (struct pse51_key is defined in tsd.c). */
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_KEYS_MAX 128

struct pse51_key;
typedef struct pse51_key *pthread_key_t;

BEGIN_C_DECLS

int pthread_key_create(pthread_key_t *key,
		       void (*destructor)(void *));

int pthread_key_delete(pthread_key_t key);

void *pthread_getspecific(pthread_key_t key);

int pthread_setspecific(pthread_key_t key,
			const void *value);

END_C_DECLS

/* One-time initialization. */
typedef struct pse51_once {
    unsigned magic;
    int routine_called;
} pthread_once_t;

#define PTHREAD_ONCE_INIT { 0x86860808, 0 }

BEGIN_C_DECLS

int pthread_once(pthread_once_t *once_control,
		 void (*init_routine)(void));

END_C_DECLS

/* Clocks and timers (yet to come). */

/* can be used as a flag for clock_nanosleep. */
#define TIMER_ABSTIME 1

BEGIN_C_DECLS

int clock_getres(clockid_t clock_id,
		 struct timespec *res);

int clock_gettime(clockid_t clock_id,
		  struct timespec *tp);

int clock_settime(clockid_t clock_id,
		  const struct timespec *tp);

int clock_nanosleep(clockid_t clock_id,
		    int flags,
                    const struct timespec *rqtp,
		    struct timespec *rmtp);

int nanosleep(const struct timespec *rqtp,
              struct timespec *rmtp);

int pthread_make_periodic_np(pthread_t thread,
			     struct timespec *starttp,
			     struct timespec *periodtp);

int pthread_wait_np(void);

int timer_create(clockid_t clockid,
		 const struct sigevent *__restrict__ evp,
		 timer_t *__restrict__ timerid);

int timer_delete(timer_t timerid);

int timer_settime(timer_t timerid,
		  int flags,
		  const struct itimerspec *__restrict__ value,
		  struct itimerspec *__restrict__ ovalue);

int timer_gettime(timer_t timerid, struct itimerspec *value);

int timer_getoverrun(timer_t timerid);

END_C_DECLS


/* Message queues. */
struct mq_attr {
    long    mq_flags;
    long    mq_maxmsg;
    long    mq_msgsize;
    long    mq_curmsgs;
};

BEGIN_C_DECLS

int mq_getattr(mqd_t qd,
	       struct mq_attr *attr);

int mq_setattr(mqd_t qd,
               const struct mq_attr *__restrict__ attr,
               struct mq_attr *__restrict__ oattr);

int mq_send(mqd_t qd,
	    const char *buffer,
	    size_t len,
	    unsigned prio);

int mq_close(mqd_t qd);

ssize_t  mq_receive(mqd_t q,
		    char *buffer,
		    size_t len,
		    unsigned *prio);

ssize_t  mq_timedreceive(mqd_t q,
                         char *__restrict__ buffer,
                         size_t len,
                         unsigned *__restrict__ prio,
                         const struct timespec *__restrict__ timeout);

int mq_timedsend(mqd_t q,
                 const char *buffer,
                 size_t len,
                 unsigned prio,
                 const struct timespec *timeout);

mqd_t mq_open(const char *name,
	      int oflags,
	      ...);

int mq_unlink(const char *name);

END_C_DECLS

#endif /* __KERNEL__ || __XENO_SIM__ */

#endif /* !_XENO_SKIN_POSIX_H */
