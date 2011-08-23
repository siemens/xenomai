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
#include <copperplate/init.h>
#include <copperplate/list.h>
#include <copperplate/lock.h>

#ifdef CONFIG_XENO_COBALT

struct threadobj_corespec {
	/* Nothing specific */
};

#else  /* CONFIG_XENO_MERCURY */

#include <sys/time.h>
#include <copperplate/notifier.h>

struct threadobj_corespec {
	int prio_unlocked;
	struct notifier notifier;
	struct timespec tslice;
};

#endif /* CONFIG_XENO_MERCURY */

/* threadobj->suspend_hook(status) */
#define THREADOBJ_SUSPEND	0x1	/* About to suspend. */
#define THREADOBJ_RESUME	0x2	/* Just resumed. */

/* threadobj->status */
#define THREADOBJ_SCHEDLOCK	0x1	/* Holds the scheduler lock. */
#define THREADOBJ_ROUNDROBIN	0x2	/* Undergoes round-robin. */

#define THREADOBJ_IRQCONTEXT    ((struct threadobj *)-2UL)

struct syncobj;
struct traceobj;

struct threadobj {
	unsigned int magic;	/* Must be first. */
	pthread_t tid;
	pthread_mutex_t lock;

	void (*finalizer)(struct threadobj *thobj);
	void (*suspend_hook)(struct threadobj *thobj, int status);
	int *errno_pointer;
	int schedlock_depth;
	int lock_state;
	int status;
	pid_t cnode;

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
	} wait_u;
	void (*wait_hook)(struct threadobj *thobj, int status);

	struct threadobj_corespec core;
	struct traceobj *tracer;
	struct pvholder thread_link;
};

struct threadobj_init_data {
	unsigned int magic;
	cpu_set_t affinity;
	void (*finalizer)(struct threadobj *thobj);
	void (*wait_hook)(struct threadobj *thobj, int status);
	void (*suspend_hook)(struct threadobj *thobj, int status);
};

extern pthread_key_t threadobj_tskey;

extern int threadobj_min_prio;

extern int threadobj_max_prio;

extern int threadobj_async;

extern int threadobj_max_prio;

#ifdef __cplusplus
extern "C" {
#endif

void threadobj_init(struct threadobj *thobj,
		    struct threadobj_init_data *idata);

int threadobj_prologue(struct threadobj *thobj,
		       const char *name);

int threadobj_cancel(struct threadobj *thobj);

void threadobj_destroy(struct threadobj *thobj);

void threadobj_finalize(void *p);

int threadobj_suspend(struct threadobj *thobj);

int threadobj_resume(struct threadobj *thobj);

int threadobj_unblock(struct threadobj *thobj);

int threadobj_lock_sched(struct threadobj *thobj);

int threadobj_unlock_sched(struct threadobj *thobj);

int threadobj_set_priority(struct threadobj *thobj, int prio);

int threadobj_get_priority(struct threadobj *thobj);

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum);

int threadobj_start_rr(struct timespec *quantum);

void threadobj_stop_rr(void);

void threadobj_pkg_init(void);

#ifdef __cplusplus
}
#endif

static inline int threadobj_lock(struct threadobj *thobj)
{
	return write_lock_safe(&thobj->lock, thobj->lock_state);
}

static inline int threadobj_trylock(struct threadobj *thobj)
{
	return write_trylock_safe(&thobj->lock, thobj->lock_state);
}

static inline int threadobj_unlock(struct threadobj *thobj)
{
	return write_unlock_safe(&thobj->lock, thobj->lock_state);
}

static inline struct threadobj *threadobj_current(void)
{
	return pthread_getspecific(threadobj_tskey);
}

static inline int threadobj_async_p(void)
{
	struct threadobj *thobj = threadobj_current();
	return thobj == THREADOBJ_IRQCONTEXT;
}

static inline int threadobj_lock_sched_once(struct threadobj *thobj)
{
	if (thobj->schedlock_depth == 0)
		return threadobj_lock_sched(thobj);

	return 0;
}

static inline int threadobj_sleep(struct timespec *ts,
				  struct timespec *ts_r)
{
	/*
	 * XXX: guaranteed to return -EINTR upon threadobj_unblock()
	 * with both Cobalt and Mercury cores.
	 */
	return -clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, ts, ts_r);
}

static inline int threadobj_context_p(void)
{
	return !threadobj_async_p() && threadobj_current();
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

static inline int threadobj_irq_priority(void)
{
	return threadobj_max_prio;
}

#ifdef CONFIG_XENO_PSHARED
static inline int threadobj_local_p(struct threadobj *thobj)
{
	return thobj->cnode == __this_node.id;
}
#else
static inline int threadobj_local_p(struct threadobj *thobj)
{
	return 1;
}
#endif

#endif /* _COPPERPLATE_THREADOBJ_H */
