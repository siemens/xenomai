/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _COBALT_KERNEL_SHADOW_H
#define _COBALT_KERNEL_SHADOW_H

#include <linux/signal.h>
#include <linux/sched.h>
#include <asm/xenomai/syscall.h>
#include <cobalt/uapi/kernel/types.h>
#include <cobalt/uapi/signal.h>

struct xnthread;
struct xnthread_user_window;
struct pt_regs;
struct timespec;
struct timeval;
struct completion;
struct module;

struct xnshadow_process;

struct xnpersonality {
	const char *name;
	unsigned int magic;
	int muxid;
	int nrcalls;
	struct xnsyscall *syscalls;
	atomic_t refcnt;
	struct {
		void *(*attach_process)(void);
		void (*detach_process)(void *arg);
		void (*map_thread)(struct xnthread *thread);
		struct xnpersonality *(*relax_thread)(struct xnthread *thread);
		struct xnpersonality *(*harden_thread)(struct xnthread *thread);
		struct xnpersonality *(*move_thread)(struct xnthread *thread,
						     int dest_cpu);
		struct xnpersonality *(*exit_thread)(struct xnthread *thread);
		struct xnpersonality *(*finalize_thread)(struct xnthread *thread);
	} ops;
	struct module *module;
};

static inline struct xnthread *xnshadow_current(void)
{
	return ipipe_current_threadinfo()->thread;
}

static inline struct xnthread *xnshadow_thread(struct task_struct *p)
{
	return ipipe_task_threadinfo(p)->thread;
}

static inline struct xnshadow_process *xnshadow_current_process(void)
{
	return ipipe_current_threadinfo()->process;
}

static inline struct xnshadow_process *
xnshadow_set_process(struct xnshadow_process *process)
{
	struct ipipe_threadinfo *p = ipipe_current_threadinfo();
	struct xnshadow_process *old;

	old = p->process;
	p->process = process;

	return old;
}

int xnshadow_mount(void);

void xnshadow_cleanup(void);

void xnshadow_grab_events(void);

void xnshadow_release_events(void);

int xnshadow_map_user(struct xnthread *thread,
		      unsigned long __user *u_window_offset);

int xnshadow_map_kernel(struct xnthread *thread,
			struct completion *done);

void xnshadow_finalize(struct xnthread *thread);

int xnshadow_harden(void);

void xnshadow_relax(int notify, int reason);

int xnshadow_register_personality(struct xnpersonality *personality);

int xnshadow_unregister_personality(int muxid);

void xnshadow_send_sig(struct xnthread *thread,
		       int sig,
		       int arg);

void xnshadow_call_mayday(struct xnthread *thread, int reason);

void __xnshadow_kick(struct xnthread *thread);

void xnshadow_kick(struct xnthread *thread);

void __xnshadow_demote(struct xnthread *thread);

void xnshadow_demote(struct xnthread *thread);

struct xnpersonality *
xnshadow_push_personality(int muxid);

void xnshadow_pop_personality(struct xnpersonality *prev);

int xnshadow_yield(xnticks_t min, xnticks_t max);

extern struct xnpersonality xenomai_personality;

#endif /* !_COBALT_KERNEL_SHADOW_H */
