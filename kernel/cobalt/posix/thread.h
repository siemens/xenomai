/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#ifndef _COBALT_POSIX_THREAD_H
#define _COBALT_POSIX_THREAD_H

#include <stdarg.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/shadow.h>
#include <cobalt/uapi/thread.h>
#include <cobalt/uapi/sched.h>

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_INHERIT_SCHED  0
#define PTHREAD_EXPLICIT_SCHED 1

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    0

struct cobalt_thread;
struct cobalt_threadstat;

/*
 * pthread_mutexattr_t and pthread_condattr_t fit on 32 bits, for
 * compatibility with libc.
 */

/* The following definitions are copied from linuxthread pthreadtypes.h. */
struct _pthread_fastlock {
	long int __status;
	int __spinlock;
};

typedef struct {
	struct _pthread_fastlock __c_lock;
	long __c_waiting;
	char __padding[48 - sizeof (struct _pthread_fastlock)
		       - sizeof (long) - sizeof (long long)];
	long long __align;
} pthread_cond_t;

enum {
	PTHREAD_PRIO_NONE,
	PTHREAD_PRIO_INHERIT,
	PTHREAD_PRIO_PROTECT
};

typedef struct {
	int __m_reserved;
	int __m_count;
	long __m_owner;
	int __m_kind;
	struct _pthread_fastlock __m_lock;
} pthread_mutex_t;

struct cobalt_local_hkey {
	/** pthread_t from userland. */
	unsigned long u_pth;
	/** kernel mm context. */
	struct mm_struct *mm;
};

struct cobalt_thread {
	unsigned int magic;
	struct xnthread threadbase;
	struct cobalt_extref extref;
	struct cobalt_process *process;

	/** cobalt_threadq */
	struct list_head link;
	struct list_head *container;

	/** Signal management. */
	sigset_t sigpending;
	struct list_head sigqueues[_NSIG]; /* cobalt_sigpending */
	struct xnsynch sigwait;
	struct list_head signext;

	/** Cached value for current policy (user side). */
	int sched_u_policy;

	/** Monitor wait object and link holder. */
	struct xnsynch monitor_synch;
	struct list_head monitor_link;
	int monitor_queued;

	struct cobalt_local_hkey hkey;
};

struct cobalt_sigwait_context {
	struct xnthread_wait_context wc;
	sigset_t *set;
	struct siginfo *si;
};

static inline struct cobalt_thread *cobalt_current_thread(void)
{
	struct xnthread *curr = xnshadow_current();
	return curr ? container_of(curr, struct cobalt_thread, threadbase) : NULL;
}

struct cobalt_thread *cobalt_thread_find(pid_t pid);

struct cobalt_thread *cobalt_thread_find_local(pid_t pid);

struct cobalt_thread *cobalt_thread_lookup(unsigned long pth);

int cobalt_thread_create(unsigned long tid, int policy,
			 struct sched_param_ex __user *u_param,
			 unsigned long __user *u_window_offset);

struct cobalt_thread *
cobalt_thread_shadow(struct task_struct *p,
		     struct cobalt_local_hkey *lhkey,
		     unsigned long __user *u_window_offset);

int cobalt_thread_make_periodic_np(unsigned long tid,
				   clockid_t clk_id,
				   struct timespec __user *u_startt,
				   struct timespec __user *u_periodt);

int cobalt_thread_wait_np(unsigned long __user *u_overruns);

int cobalt_thread_set_mode_np(int clrmask, int setmask, int __user *u_mode_r);

int cobalt_thread_set_name_np(unsigned long tid, const char __user *u_name);

int cobalt_thread_probe_np(pid_t h_tid);

int cobalt_thread_kill(unsigned long tid, int sig);

int cobalt_thread_join(unsigned long tid);

int cobalt_thread_stat(pid_t pid,
		       struct cobalt_threadstat __user *u_stat);

int cobalt_thread_setschedparam_ex(unsigned long tid,
				   int policy,
				   struct sched_param_ex __user *u_param,
				   unsigned long __user *u_window_offset,
				   int __user *u_promoted);

int cobalt_thread_getschedparam_ex(unsigned long tid,
				   int __user *u_policy,
				   struct sched_param_ex __user *u_param);

int cobalt_sched_yield(void);

int cobalt_sched_min_prio(int policy);

int cobalt_sched_max_prio(int policy);

int cobalt_sched_setconfig_np(int cpu,
			      int policy,
			      const union sched_config __user *u_config,
			      size_t len);

ssize_t cobalt_sched_getconfig_np(int cpu,
				  int policy,
				  union sched_config __user *u_config,
				  size_t len);

struct xnpersonality *cobalt_thread_map(struct xnthread *curr);

struct xnpersonality *cobalt_thread_exit(struct xnthread *curr);

struct xnpersonality *cobalt_thread_finalize(struct xnthread *zombie);

#ifdef CONFIG_XENO_OPT_COBALT_EXTENSION

void cobalt_thread_extend(struct cobalt_thread *thread,
			  struct cobalt_extension *ext,
			  void *priv);

void cobalt_thread_restrict(struct cobalt_thread *thread);

static inline
int cobalt_thread_extended_p(const struct cobalt_thread *thread,
			     const struct cobalt_extension *ext)
{
	return thread->extref.extension == ext;
}

#else /* !CONFIG_XENO_OPT_COBALT_EXTENSION */

static inline
int cobalt_thread_extended_p(const struct cobalt_thread *thread,
			     const struct cobalt_extension *ext)
{
	return 0;
}

#endif /* !CONFIG_XENO_OPT_COBALT_EXTENSION */

extern xnticks_t cobalt_time_slice;

extern struct xnpersonality cobalt_personality;

#endif /* !_COBALT_POSIX_THREAD_H */
