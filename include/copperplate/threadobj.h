/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COPPERPLATE_THREADOBJ_H
#define _COPPERPLATE_THREADOBJ_H

#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <copperplate/list.h>
#include <copperplate/lock.h>
#include <copperplate/clockobj.h>
#include <copperplate/heapobj.h>
#include <copperplate/debug.h>

#ifdef CONFIG_XENO_COBALT

struct threadobj_corespec {
	/* Nothing specific. */
};

struct threadobj_stat {
	/** Cobalt thread status bits. */
	unsigned long status;
	/** Execution time in primary mode (ns). */
	ticks_t xtime;
	/** Number of primary->secondary mode switches. */
	int msw;
	/** Number of context switches. */
	int csw;
	/** Number of page faults. */
	int pf;
};

#define SCHED_RT  SCHED_COBALT

#else  /* CONFIG_XENO_MERCURY */

#include <sys/time.h>
#include <copperplate/notifier.h>

struct threadobj_corespec {
	int prio_unlocked;
	struct notifier notifier;
	struct timespec tslice;
	struct timespec wakeup;
	ticks_t period;
};

struct threadobj_stat {
	/** Mercury thread status bits. */
	unsigned long status;
};

#define SCHED_RT  SCHED_FIFO

#endif /* CONFIG_XENO_MERCURY */

/* threadobj->suspend_hook(event) */
#define THREADOBJ_SUSPEND	0x1	/* Just suspended. */
#define THREADOBJ_RESUME	0x2	/* About to resume. */

/* threadobj->status */
#define THREADOBJ_SCHEDLOCK	0x1	/* Holds the scheduler lock. */
#define THREADOBJ_ROUNDROBIN	0x2	/* Undergoes round-robin. */
#define THREADOBJ_STARTED	0x4	/* threadobj_start() called. */
#define THREADOBJ_WARMUP	0x8	/* threadobj_prologue() not called yet. */
#define THREADOBJ_ABORTED	0x10	/* cancelled before start. */
#define THREADOBJ_DEBUG		0x8000	/* Debug mode enabled. */

#define THREADOBJ_IRQCONTEXT    ((struct threadobj *)-2UL)

/* threadobj mode bits */
#define __THREAD_M_LOCK		0x80000000 /* Toggle scheduler lock. */
#define __THREAD_M_WARNSW	0x40000000 /* Toggle switch warning bit. */
#define __THREAD_M_CONFORMING	0x20000000 /* Switch to conforming mode. */
#define __THREAD_M_SPARESTART	0
#define __THREAD_M_SPARECOUNT	12
#define __THREAD_M_SPARE0	0x00000001
#define __THREAD_M_SPARE1	0x00000002
#define __THREAD_M_SPARE2	0x00000004
#define __THREAD_M_SPARE3	0x00000008
#define __THREAD_M_SPARE4	0x00000010
#define __THREAD_M_SPARE5	0x00000020
#define __THREAD_M_SPARE6	0x00000040
#define __THREAD_M_SPARE7	0x00000080
#define __THREAD_M_SPARE8	0x00000100
#define __THREAD_M_SPARE9	0x00000200
#define __THREAD_M_SPARE10	0x00000400
#define __THREAD_M_SPARE11	0x00000800

struct traceobj;
struct syncobj;

struct threadobj {
	unsigned int magic;	/* Must be first. */
	pthread_t tid;
	pthread_mutex_t lock;

	void (*finalizer)(struct threadobj *thobj);
	void (*suspend_hook)(struct threadobj *thobj, int status);
	int *errno_pointer;
	int schedlock_depth;
	int cancel_state;
	int status;
	int priority;
	pid_t cnode;
	const char *name;

	/* Those members belong exclusively to the syncobj code. */
	struct syncobj *wait_sobj;
	pthread_cond_t wait_sync;
	struct holder wait_link;
	int wait_status;
	int wait_prio;
	union {
		struct {
			void *ptr;
			size_t size;
		} buffer;
	} wait_u;	/* XXX: deprecated by wait_union */
	void (*wait_hook)(struct threadobj *thobj, int status);
	void *wait_union;
	size_t wait_size;

	struct threadobj_corespec core;
	pthread_cond_t barrier;
	struct traceobj *tracer;
	struct pvholder thread_link;
	struct backtrace_data btd;
};

struct threadobj_init_data {
	unsigned int magic;
	cpu_set_t affinity;
	int priority;
	void (*finalizer)(struct threadobj *thobj);
	void (*wait_hook)(struct threadobj *thobj, int status);
	void (*suspend_hook)(struct threadobj *thobj, int status);
};

extern pthread_key_t threadobj_tskey;

extern int threadobj_high_prio;

extern int threadobj_irq_prio;

#ifdef __cplusplus
extern "C" {
#endif

void *__threadobj_alloc(size_t tcb_struct_size,
			size_t wait_union_size,
			int thobj_offset);

static inline void threadobj_free(void *p)
{
	xnfree(p);
}

void threadobj_init(struct threadobj *thobj,
		    struct threadobj_init_data *idata);

void threadobj_start(struct threadobj *thobj);

int threadobj_prologue(struct threadobj *thobj,
		       const char *name);

void threadobj_wait_start(struct threadobj *thobj);

int threadobj_cancel(struct threadobj *thobj);

void threadobj_destroy(struct threadobj *thobj);

void threadobj_finalize(void *p);

int threadobj_suspend(struct threadobj *thobj);

int threadobj_resume(struct threadobj *thobj);

int threadobj_unblock(struct threadobj *thobj);

int threadobj_lock_sched(struct threadobj *thobj);

int threadobj_unlock_sched(struct threadobj *thobj);

int threadobj_set_priority(struct threadobj *thobj, int prio);

int threadobj_set_mode(struct threadobj *thobj,
		       int clrmask, int setmask, int *mode_r);

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum);

int threadobj_start_rr(struct timespec *quantum);

void threadobj_stop_rr(void);

int threadobj_set_periodic(struct threadobj *thobj,
			   struct timespec *idate, struct timespec *period);

int threadobj_wait_period(struct threadobj *thobj,
			  unsigned long *overruns_r);

void threadobj_spin(ticks_t ns);

int threadobj_stat(struct threadobj *thobj,
		   struct threadobj_stat *stat);

#ifdef CONFIG_XENO_PSHARED

int __threadobj_local_p(struct threadobj *thobj);

static inline int threadobj_local_p(struct threadobj *thobj)
{
	return __threadobj_local_p(thobj);
}

#else

static inline int threadobj_local_p(struct threadobj *thobj)
{
	return 1;
}

#endif	/* !CONFIG_XENO_PSHARED */

void threadobj_pkg_init(void);

#ifdef __cplusplus
}
#endif

#define threadobj_alloc(T, __mptr, W)					\
	({								\
		void *__p;						\
		__p = __threadobj_alloc(sizeof(T), sizeof(W), offsetof(T, __mptr)); \
		__p;							\
	})

static inline int threadobj_get_priority(struct threadobj *thobj)
{
	return thobj->priority;
}

static inline int threadobj_lock(struct threadobj *thobj)
{
	return write_lock_safe(&thobj->lock, thobj->cancel_state);
}

static inline int threadobj_trylock(struct threadobj *thobj)
{
	return write_trylock_safe(&thobj->lock, thobj->cancel_state);
}

static inline int threadobj_unlock(struct threadobj *thobj)
{
	return write_unlock_safe(&thobj->lock, thobj->cancel_state);
}

static inline struct threadobj *threadobj_current(void)
{
	return pthread_getspecific(threadobj_tskey);
}

static inline int threadobj_irq_p(void)
{
	struct threadobj *current = threadobj_current();
	return current == THREADOBJ_IRQCONTEXT;
}

static inline int threadobj_current_p(void)
{
	struct threadobj *current = threadobj_current();
	return current && current != THREADOBJ_IRQCONTEXT;
}

static inline int threadobj_lock_sched_once(struct threadobj *thobj)
{
	if (thobj->schedlock_depth == 0)
		return threadobj_lock_sched(thobj);

	return 0;
}

static inline void threadobj_yield(void)
{
	__RT(sched_yield());
}

static inline int threadobj_sleep(struct timespec *ts)
{
	/*
	 * XXX: guaranteed to return -EINTR upon threadobj_unblock()
	 * with both Cobalt and Mercury cores.
	 */
	return -__RT(clock_nanosleep(CLOCK_COPPERPLATE, TIMER_ABSTIME, ts, NULL));
}

static inline unsigned int threadobj_get_magic(struct threadobj *thobj)
{
	return thobj->magic;
}

static inline void threadobj_set_magic(struct threadobj *thobj,
				       unsigned int magic)
{
	thobj->magic = magic;
}

static inline int threadobj_get_lockdepth(struct threadobj *thobj)
{
	return thobj->schedlock_depth;
}

static inline int threadobj_get_status(struct threadobj *thobj)
{
	return thobj->status;
}

static inline int threadobj_get_errno(struct threadobj *thobj)
{
	return *thobj->errno_pointer;
}

#define threadobj_prepare_wait(T)					\
	({								\
		struct threadobj *__thobj = threadobj_current();	\
		assert(__thobj != NULL);				\
		assert(sizeof(typeof(T)) <= __thobj->wait_size);	\
		__thobj->wait_union;					\
	})

#define threadobj_finish_wait()		do { } while (0)

static inline void *threadobj_get_wait(struct threadobj *thobj)
{
	return thobj->wait_union;
}

static inline const char *threadobj_get_name(struct threadobj *thobj)
{
	return thobj->name;
}

#endif /* _COPPERPLATE_THREADOBJ_H */
