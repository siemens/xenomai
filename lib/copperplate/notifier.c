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
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include "copperplate/notifier.h"
#include "boilerplate/lock.h"
#include "boilerplate/signal.h"
#include "copperplate/debug.h"
#include "internal.h"

static DEFINE_PRIVATE_LIST(notifier_list);

static pthread_mutex_t notifier_lock;

static struct sigaction notifier_old_sa;

static void notifier_sighandler(int sig, siginfo_t *siginfo, void *uc)
{
	struct notifier *nf;
	pid_t tid;

	tid = copperplate_get_tid();

	if (pvlist_empty(&notifier_list))
		goto ouch;

	/* We may NOT alter the notifier list, but only scan it. */
	pvlist_for_each_entry(nf, &notifier_list, link) {
		if (nf->owner == tid) {
			notifier_wait(nf);
			return;
		}
	}
ouch:
	panic("received spurious notification on thread[%d] "
	      "(sig=%d, code=%d, fd=%d)",
	      tid, sig, siginfo->si_code, siginfo->si_fd);
}

static void lock_notifier_list(sigset_t *oset)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGNOTIFY);
	pthread_sigmask(SIG_BLOCK, &set, oset);
	write_lock(&notifier_lock);
}

static void unlock_notifier_list(sigset_t *oset)
{
	pthread_sigmask(SIG_SETMASK, oset, NULL);
	write_unlock(&notifier_lock);
}

int notifier_init(struct notifier *nf, pid_t pid)
{
	sigset_t oset;
	int ret;

	if (pipe(nf->waitfd) < 0) {
		ret = -errno;
		goto fail;
	}

	nf->owner = pid;

	push_cleanup_lock(&notifier_lock);
	lock_notifier_list(&oset);
	pvlist_append(&nf->link, &notifier_list);
	unlock_notifier_list(&oset);
	pop_cleanup_lock(&notifier_lock);

	return 0;
fail:
	warning("failed to create notifier pipe");

	return __bt(ret);
}

void notifier_destroy(struct notifier *nf)
{
	sigset_t oset;

	push_cleanup_lock(&notifier_lock);
	lock_notifier_list(&oset);
	pvlist_remove(&nf->link);
	unlock_notifier_list(&oset);
	pop_cleanup_lock(&notifier_lock);
	close(nf->waitfd[0]); /* May fail if disabled. */
	close(nf->waitfd[1]);
}

void notifier_signal(struct notifier *nf)
{
	copperplate_kill_tid(nf->owner, SIGNOTIFY);
}

void notifier_disable(struct notifier *nf)
{
	close(nf->waitfd[0]);
}

void notifier_release(struct notifier *nf)
{
	char c = 1;
	int ret;

	do
		ret = write(nf->waitfd[1], &c, 1);
	while (ret == -1 && errno == EINTR);
}

void notifier_wait(const struct notifier *nf)
{
	int ret;
	char c;

	do
		ret = read(nf->waitfd[0], &c, 1);
	while (ret == -1 && errno == EINTR);
}

void notifier_pkg_init(void)
{
	pthread_mutexattr_t mattr;
	struct sigaction sa;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutex_init(&notifier_lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
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
	sa.sa_sigaction = &notifier_sighandler;
	sa.sa_flags = SA_SIGINFO|SA_RESTART;
	sigaction(SIGNOTIFY, &sa, &notifier_old_sa);
}
