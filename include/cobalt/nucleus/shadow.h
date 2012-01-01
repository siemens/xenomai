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

/* Events sent to the interface callback */
#define XNSHADOW_CLIENT_ATTACH  0
#define XNSHADOW_CLIENT_DETACH  1

#define LO_START_REQ    0
#define LO_WAKEUP_REQ   1
#define LO_SIGGRP_REQ   2
#define LO_SIGTHR_REQ   3
#define LO_UNMAP_REQ    4
#define LO_FREEMEM_REQ  5

struct xnthread;
struct xnmutex;
struct pt_regs;
struct timespec;
struct timeval;

struct xnskin_props {
	const char *name;
	unsigned magic;
	int nrcalls;
	void *(*eventcb)(int, void *);
	struct xnsysent *systab;
};

static inline struct xnthread *xnshadow_current(void)
{
	return ipipe_current_threadinfo()->thread;
}

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

int xnshadow_map(struct xnthread *thread,
		 xncompletion_t __user *u_completion,
		 unsigned long __user *u_mode_offset);

void xnshadow_unmap(struct xnthread *thread);

int xnshadow_harden(void);

void xnshadow_relax(int notify, int reason);

void xnshadow_renice(struct xnthread *thread);

void xnshadow_suspend(struct xnthread *thread);

void xnshadow_start(struct xnthread *thread);

void xnshadow_signal_completion(xncompletion_t __user *u_completion,
				int err);

void xnshadow_exit(void);

int xnshadow_register_interface(struct xnskin_props *props);

int xnshadow_unregister_interface(int muxid);

void xnshadow_reset_shield(void);

void xnshadow_send_sig(struct xnthread *thread,
		       int sig,
		       int arg,
		       int specific);

void xnshadow_call_mayday(struct xnthread *thread, int sigtype);

void xnshadow_kick(struct xnthread *thread);

void xnshadow_demote(struct xnthread *thread);

void xnshadow_post_linux(int type, void *ptr, size_t val);

#endif /* !_XENO_NUCLEUS_SHADOW_H */
