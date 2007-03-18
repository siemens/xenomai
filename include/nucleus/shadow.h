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
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */

#define XENOMAI_MUX_NR 16

/* Events sent to the interface callback */
#define XNSHADOW_CLIENT_ATTACH  0
#define XNSHADOW_CLIENT_DETACH  1

#ifdef __cplusplus
extern "C" {
#endif

struct xnthread;
struct xnmutex;
struct pt_regs;
struct timespec;
struct timeval;

struct xnskentry {

	const char *name;
	unsigned magic;
	int nrcalls;
	atomic_counter_t refcnt;
	void *(*eventcb)(int, void *);
	xnsysent_t *systab;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc;
#endif /* CONFIG_PROC_FS */
	struct module *module;
};

int xnshadow_mount(void);

void xnshadow_cleanup(void);

void xnshadow_grab_events(void);

void xnshadow_release_events(void);

int xnshadow_map(struct xnthread *thread,
		 xncompletion_t __user *u_completion);

void xnshadow_unmap(struct xnthread *thread);

int xnshadow_harden(void);

void xnshadow_relax(int notify);

void xnshadow_renice(struct xnthread *thread);

void xnshadow_suspend(struct xnthread *thread);

int xnshadow_wait_barrier(struct pt_regs *regs);

void xnshadow_start(struct xnthread *thread);

void xnshadow_signal_completion(xncompletion_t __user *u_completion,
				int err);

void xnshadow_exit(void);

int xnshadow_register_interface(const char *name,
				unsigned magic,
				int nrcalls,
				xnsysent_t *systab,
				void *(*eventcb)(int event, void *data),
				struct module *module);

int xnshadow_unregister_interface(int muxid);

unsigned long long xnshadow_ts2ticks(const struct timespec *v);

void xnshadow_ticks2ts(unsigned long long,
		       struct timespec *v);

unsigned long long xnshadow_tv2ticks(const struct timeval *v);

void xnshadow_ticks2tv(unsigned long long ticks,
		       struct timeval *v);

void xnshadow_reset_shield(void);

void xnshadow_send_sig(struct xnthread *thread,
		       int sig,
		       int specific);

void xnshadow_rpi_check(void);

extern struct xnskentry muxtable[];

#ifdef __cplusplus
}
#endif

#endif /* !_XENO_NUCLEUS_SHADOW_H */
