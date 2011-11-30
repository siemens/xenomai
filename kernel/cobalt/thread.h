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


#ifndef _POSIX_THREAD_H
#define _POSIX_THREAD_H

#include "internal.h"

typedef unsigned long long cobalt_sigset_t;

struct mm_struct;

struct cobalt_hkey {
	unsigned long u_tid;
	struct mm_struct *mm;
};

struct cobalt_hash {
	pthread_t k_tid;	/* Xenomai in-kernel (nucleus) tid */
	pid_t h_tid;		/* Host (linux) tid */
	struct cobalt_hkey hkey;
	struct cobalt_hash *next;
};

typedef struct {
	cobalt_sigset_t mask;
	xnpqueue_t list;
} cobalt_sigqueue_t;

struct cobalt_thread {
	unsigned magic;
	xnthread_t threadbase;

#define thread2pthread(taddr) ({                                        \
			xnthread_t *_taddr = (taddr);			\
			(_taddr						\
			 ? ((xnthread_get_magic(_taddr) == COBALT_SKIN_MAGIC) \
			    ? ((pthread_t)(((char *)_taddr)- offsetof(struct cobalt_thread, \
								      threadbase))) \
			    : NULL)					\
			 : NULL);					\
		})


	xnholder_t link;	/* Link in cobalt_threadq */
	xnqueue_t *container;

#define link2pthread(laddr) container_of(laddr, struct cobalt_thread, link)

	pthread_attr_t attr;        /* creation attributes */

	void *(*entry)(void *arg);  /* start routine */
	void *arg;                  /* start routine argument */

	/* For pthread_join */
	void *exit_status;
	xnsynch_t join_synch;       /* synchronization object, used by other threads
				       waiting for this one to finish. */
	int nrt_joiners;

	/* For pthread_cancel */
	unsigned cancelstate : 2;
	unsigned canceltype : 2;
	unsigned cancel_request : 1;
	xnqueue_t cleanup_handlers_q;

	/* errno value for this thread. */
	int err;

	/* For signals handling. */
	cobalt_sigset_t sigmask;     /* signals mask. */
	cobalt_sigqueue_t pending;   /* Pending signals */
	cobalt_sigqueue_t blocked_received; /* Blocked signals received. */

	/* For thread specific data. */
	const void *tsd [PTHREAD_KEYS_MAX];

	/* For timers. */
	xnqueue_t timersq;

	/* Cached value for current policy. */
	int sched_policy;

	/* Monitor wait object and link holder. */
	struct xnsynch monitor_synch;
	struct xnholder monitor_link;

#ifndef __XENO_SIM__
	struct cobalt_hkey hkey;
#endif
};

#define COBALT_JOINED_DETACHED XNTHREAD_INFO_SPARE0

#define cobalt_current_thread() thread2pthread(xnpod_current_thread())

static inline void thread_set_errno (int err)
{
	*xnthread_get_errno_location(xnpod_current_thread()) = err;
}

static inline int thread_get_errno (void)
{
	return *xnthread_get_errno_location(xnpod_current_thread());
}

#define thread_name(thread) ((thread)->attr.name)

#define thread_exit_status(thread) ((thread)->exit_status)

#define thread_getdetachstate(thread) ((thread)->attr.detachstate)

#define thread_setdetachstate(thread, state) ((thread)->attr.detachstate=state)

#define thread_getcancelstate(thread) ((thread)->cancelstate)

#define thread_setcancelstate(thread, state) ((thread)->cancelstate=state)

#define thread_setcanceltype(thread, type) ((thread)->canceltype=type)

#define thread_getcanceltype(thread) ((thread)->canceltype)

#define thread_clrcancel(thread) ((thread)->cancel_request = 0)

#define thread_setcancel(thread) ((thread)->cancel_request = 1)

#define thread_cleanups(thread) (&(thread)->cleanup_handlers_q)

#define thread_gettsd(thread, key) ((thread)->tsd[key])

#define thread_settsd(thread, key, value) ((thread)->tsd[key]=(value))

void cobalt_thread_abort(pthread_t thread, void *status);

struct cobalt_hash *cobalt_thread_hash(const struct cobalt_hkey *hkey,
				       pthread_t k_tid,
				       pid_t h_tid);

void cobalt_thread_unhash(const struct cobalt_hkey *hkey);

pthread_t cobalt_thread_find(const struct cobalt_hkey *hkey);

int cobalt_thread_probe(pid_t h_tid);

static inline void thread_cancellation_point (xnthread_t *thread)
{
    pthread_t cur = thread2pthread(thread);

    if(cur && cur->cancel_request
	&& thread_getcancelstate(cur) == PTHREAD_CANCEL_ENABLE )
	cobalt_thread_abort(cur, PTHREAD_CANCELED);
}

void cobalt_threadq_cleanup(cobalt_kqueues_t *q);

void cobalt_thread_pkg_init(u_long rrperiod);

void cobalt_thread_pkg_cleanup(void);

/* round-robin period. */
extern xnticks_t cobalt_time_slice;

#endif /* !_POSIX_THREAD_H */
