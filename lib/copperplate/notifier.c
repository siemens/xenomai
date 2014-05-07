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

#include <signal.h>
#include <memory.h>
#include <errno.h>
#include "copperplate/notifier.h"
#include "boilerplate/signal.h"
#include "internal.h"

static void suspend_sighandler(int sig)
{
	notifier_wait();
}

static void resume_sighandler(int sig)
{
	/* nop */
}

int notifier_init(struct notifier *nf, pid_t pid)
{
	sigset_t set;

	nf->owner = pid;
	sigemptyset(&set);
	sigaddset(&set, SIGRESM);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	return 0;
}

void notifier_signal(struct notifier *nf)
{
	copperplate_kill_tid(nf->owner, SIGSUSP);
}

void notifier_release(struct notifier *nf)
{
	copperplate_kill_tid(nf->owner, SIGRESM);
}

void notifier_wait(void)
{
	sigset_t set;

	/*
	 * A suspended thread is supposed to do nothing but wait for
	 * the wake up signal, so we may happily block all signals but
	 * SIGRESM. Note that SIGRRB won't be accumulated during the
	 * sleep time anyhow, as the round-robin timer is based on
	 * CLOCK_THREAD_CPUTIME_ID, and we'll obviously don't consume
	 * any CPU time while blocked.
	 */
	sigfillset(&set);
	sigdelset(&set, SIGRESM);
	sigsuspend(&set);
}

void notifier_disable(struct notifier *nf)
{
	/* Unblock any ongoing wait. */
	copperplate_kill_tid(nf->owner, SIGRESM);
}

void notifier_pkg_init(void)
{
	struct sigaction sa;
	/*
	 * We have two basic requirements for the notification
	 * scheme implementing the suspend/resume mechanism:
	 *
	 * - we have to rely on Linux signals for notification, which
	 * guarantees that the target thread will receive the suspend
	 * request asap, regardless of what it was doing when notified
	 * (syscall wait, pure runtime etc.).
	 *
	 * - we must process the suspension signal on behalf of the
	 * target thread, as we want that thread to block upon
	 * receipt.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = suspend_sighandler;
	sigaction(SIGSUSP, &sa, NULL);
	sa.sa_handler = resume_sighandler;
	sigaction(SIGRESM, &sa, NULL);
}
