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

#define thread2pthread(taddr) \
	({								\
		xnthread_t *_taddr = (taddr);				\
		(_taddr							\
		 ? ((xnthread_get_magic(_taddr) == COBALT_SKIN_MAGIC)	\
		    ? (container_of(_taddr, struct cobalt_thread, threadbase)) \
		    : NULL)						\
		 : NULL);						\
	})


	xnholder_t link;	/* Link in cobalt_threadq */
	xnqueue_t *container;

#define link2pthread(laddr) container_of(laddr, struct cobalt_thread, link)

	pthread_attr_t attr;        /* creation attributes */

	/* For timers. */
	xnqueue_t timersq;

	/* Cached value for current policy. */
	int sched_policy;

	/* Monitor wait object and link holder. */
	struct xnsynch monitor_synch;
	struct xnholder monitor_link;
	int monitor_queued;

#ifndef __XENO_SIM__
	struct cobalt_hkey hkey;
#endif
};

#define cobalt_current_thread() thread2pthread(xnpod_current_thread())

#define thread_name(thread) ((thread)->attr.name)

pthread_t cobalt_thread_find(const struct cobalt_hkey *hkey);

int cobalt_thread_create(unsigned long tid, int policy,
			 struct sched_param_ex __user *u_param,
			 unsigned long __user *u_mode);

pthread_t cobalt_thread_shadow(struct task_struct *p,
			       struct cobalt_hkey *hkey,
			       unsigned long __user *u_mode_offset);

int cobalt_thread_make_periodic_np(unsigned long tid,
				   clockid_t clk_id,
				   struct timespec __user *u_startt,
				   struct timespec __user *u_periodt);

int cobalt_thread_wait_np(unsigned long __user *u_overruns);

int cobalt_thread_set_mode_np(int clrmask, int setmask, int __user *u_mode_r);

int cobalt_thread_set_name_np(unsigned long tid, const char __user *u_name);

int cobalt_thread_probe_np(pid_t h_tid);

int cobalt_thread_kill(unsigned long tid, int sig);

int cobalt_thread_stat(unsigned long tid,
		       struct cobalt_threadstat __user *u_stat);

int cobalt_thread_setschedparam(unsigned long tid,
				int policy,
				struct sched_param __user *u_param,
				unsigned long __user *u_mode_offset,
				int __user *u_promoted);

int cobalt_thread_setschedparam_ex(unsigned long tid,
				   int policy,
				   struct sched_param __user *u_param,
				   unsigned long __user *u_mode_offset,
				   int __user *u_promoted);

int cobalt_thread_getschedparam(unsigned long tid,
				int __user *u_policy,
				struct sched_param __user *u_param);

int cobalt_thread_getschedparam_ex(unsigned long tid,
				   int __user *u_policy,
				   struct sched_param __user *u_param);

int cobalt_sched_yield(void);

int cobalt_sched_min_prio(int policy);

int cobalt_sched_max_prio(int policy);

void cobalt_threadq_cleanup(cobalt_kqueues_t *q);

void cobalt_thread_pkg_init(u_long rrperiod);

void cobalt_thread_pkg_cleanup(void);

/* round-robin period. */
extern xnticks_t cobalt_time_slice;

#endif /* !_POSIX_THREAD_H */
