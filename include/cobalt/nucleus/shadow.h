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

#ifndef _XENO_NUCLEUS_SHADOW_H
#define _XENO_NUCLEUS_SHADOW_H

#include <asm/xenomai/atomic.h>
#include <asm/xenomai/syscall.h>

#define XENOMAI_SKINS_NR  4

struct xnthread;
struct xnthread_user_window;
struct xnmutex;
struct pt_regs;
struct timespec;
struct timeval;
struct completion;
struct xnshadow_ppd;

struct xnskin_client_ops {
	struct xnshadow_ppd *(*attach)(void);
	void (*detach)(struct xnshadow_ppd *ppd);
};

struct xnskin_props {
	const char *name;
	unsigned int magic;
	int nrcalls;
	struct xnsysent *systab;
	struct xnskin_client_ops ops;
};

static inline struct xnthread *xnshadow_current(void)
{
	return ipipe_current_threadinfo()->thread;
}

#define xnshadow_current_p(thread) (xnshadow_current() == (thread))

static inline struct xnthread *xnshadow_thread(struct task_struct *p)
{
	return ipipe_task_threadinfo(p)->thread;
}

static inline struct mm_struct *xnshadow_current_mm(void)
{
	return ipipe_current_threadinfo()->mm;
}

static inline struct mm_struct *xnshadow_swap_mm(struct mm_struct *mm)
{
	struct ipipe_threadinfo *p = ipipe_current_threadinfo();
	struct mm_struct *oldmm;

	oldmm = p->mm;
	p->mm = mm;

	return oldmm;
}

int xnshadow_mount(void);

void xnshadow_cleanup(void);

void xnshadow_grab_events(void);

void xnshadow_release_events(void);

int xnshadow_map_user(struct xnthread *thread,
		      unsigned long __user *u_window_offset);

int xnshadow_map_kernel(struct xnthread *thread,
			struct completion *done);

void xnshadow_unmap(struct xnthread *thread);

int xnshadow_harden(void);

void xnshadow_relax(int notify, int reason);

int xnshadow_register_interface(struct xnskin_props *props);

int xnshadow_unregister_interface(int muxid);

void xnshadow_reset_shield(void);

void xnshadow_send_sig(struct xnthread *thread,
		       int sig,
		       int arg);

void xnshadow_call_mayday(struct xnthread *thread, int sigtype);

void __xnshadow_kick(struct xnthread *thread);

void xnshadow_kick(struct xnthread *thread);

void __xnshadow_demote(struct xnthread *thread);

void xnshadow_demote(struct xnthread *thread);

#endif /* !_XENO_NUCLEUS_SHADOW_H */
