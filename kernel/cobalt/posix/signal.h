/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_POSIX_SIGNAL_H
#define _COBALT_POSIX_SIGNAL_H

#include <linux/signal.h>
#include <cobalt/kernel/list.h>

struct cobalt_thread;

struct cobalt_sigevent {
	siginfo_t si;
	struct list_head next;
};

int cobalt_signal_send(struct cobalt_thread *thread, siginfo_t *si);

int cobalt_signal_wait(sigset_t *set, siginfo_t *si,
		       xnticks_t timeout, xntmode_t tmode);

void cobalt_signal_flush(struct cobalt_thread *thread);

int cobalt_sigwait(const sigset_t __user *u_set, int __user *u_sig);

int cobalt_sigtimedwait(const sigset_t __user *u_set, siginfo_t __user *u_si,
			const struct timespec __user *u_timeout);

int cobalt_sigwaitinfo(const sigset_t __user *u_set,
		       siginfo_t __user *u_si);

int cobalt_sigpending(sigset_t __user *u_set);

int cobalt_signal_pkg_init(void);

void cobalt_signal_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_SIGNAL_H */
