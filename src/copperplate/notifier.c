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
#include "copperplate/panic.h"
#include "copperplate/notifier.h"
#include "copperplate/lock.h"

#include <linux/unistd.h>
#define do_gettid()	syscall(__NR_gettid)

/* Private signal used for notification. */
#define NOTIFYSIG	(SIGRTMIN + 8)

static DEFINE_PRIVATE_LIST(notifier_list);

static pthread_mutex_t notifier_lock;

static struct sigaction notifier_old_sa;

static fd_set notifier_rset;

static int max_rfildes;		/* Increases, never decreases. */

static void notifier_sighandler(int sig, siginfo_t *siginfo, void *uc)
{
	static struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
	pid_t tid = do_gettid();
	int ret, matched = 0;
	struct notifier *nf;
	fd_set rfds;
	char c;

	if (siginfo->si_code == SI_SIGIO) {
		FD_ZERO(&rfds);
		FD_SET(siginfo->si_fd, &rfds);
	} else {
		/*
		 * We will have to find out by ourselves which fd was
		 * notified.
		 */
		rfds = notifier_rset;
		ret = select(max_rfildes + 1, &rfds, NULL, NULL, &tv);
		if (ret <= 0)
			goto hand_over;
	}

	/* We may NOT alter the notifier list, but only scan it. */
	pvlist_for_each_entry(nf, &notifier_list, link) {
		if (!FD_ISSET(nf->psfd[0], &rfds))
			continue;
		/*
		 * Ignore misdirected notifications. We want those to
		 * hit the thread owning the notification object, but
		 * it may happen that the kernel picks another thread
		 * for receiving a subsequent signal while we are
		 * blocked in the callback code. In such a case, we
		 * just dismiss the notification, and expect the
		 * actual owner to detect the pending notification
		 * once the callback returns to the read() loop.
		 */
		matched = 1;
		if (nf->owner && nf->owner != tid)
			continue;

		while (read(nf->psfd[0], &c, 1) > 0)
			/* Callee must run async-safe code only. */
			nf->callback(nf);
		return;
	}

	/*
	 * We may have received a valid notification on the wrong
	 * thread: bail out silently, assuming the recipient will find
	 * out eventually.
	 */
	if (matched)
		return;

hand_over:
	if (notifier_old_sa.sa_sigaction) {
		/*
		 * This is our best effort to relay any unprocessed
		 * event to the user-defined handler for NOTIFYSIG we
		 * might have overriden in notifier_pkg_init(). The
		 * application code should set this handler prior to
		 * calling copperplate_init(), so that we know about it. The
		 * signal setup flags will be ours however
		 * (i.e. SA_SIGINFO + BSD semantics).
		 */
		notifier_old_sa.sa_sigaction(sig, siginfo, uc);
		return;
	}

	panic("received spurious notification on thread[%d] [sig=%d, code=%d, fd=%d]",
	      tid, sig, siginfo->si_code, siginfo->si_fd);
}

static void lock_notifier_list(sigset_t *oset)
{
	sigset_t set;

	read_lock(&notifier_lock);
	pthread_sigmask(SIG_BLOCK, NULL, &set);
	sigaddset(&set, NOTIFYSIG);
	pthread_sigmask(SIG_BLOCK, &set, oset);
}

static void unlock_notifier_list(sigset_t *oset)
{
	pthread_sigmask(SIG_SETMASK, oset, NULL);
	read_unlock(&notifier_lock);
}

int notifier_init(struct notifier *nf,
		  void (*callback)(const struct notifier *nf),
		  int owned)
{
	pthread_mutexattr_t mattr;
	sigset_t oset;
	int fd;

	if (pipe(nf->psfd) < 0)
		return -errno;

	if (pipe(nf->pwfd) < 0) {
		close(nf->psfd[0]);
		close(nf->psfd[1]);
		return -errno;
	}

	nf->callback = callback;
	pvholder_init(&nf->link);
	nf->notified = 0;
	nf->owner = owned ? do_gettid() : 0;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutex_init(&nf->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);

	push_cleanup_lock(&notifier_lock);
	lock_notifier_list(&oset);
	pvlist_append(&nf->link, &notifier_list);
	if (nf->psfd[0] > max_rfildes)
		max_rfildes = nf->psfd[0];
	FD_SET(nf->psfd[0], &notifier_rset);
	unlock_notifier_list(&oset);
	pop_cleanup_lock(&notifier_lock);

	fd = nf->psfd[0];
	fcntl(fd, F_SETSIG, NOTIFYSIG);
	fcntl(fd, F_SETOWN, nf->owner);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_ASYNC | O_NONBLOCK);
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
	/*
	 * Somewhat paranoid, but makes sure no flow control will ever
	 * block us when signaling a notifier object.
	 */
	fd = nf->psfd[1];
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);

	return 0;
}

void notifier_destroy(struct notifier *nf)
{
	sigset_t oset;

	push_cleanup_lock(&notifier_lock);
	lock_notifier_list(&oset);
	pvlist_remove_init(&nf->link);
	FD_CLR(nf->psfd[0], &notifier_rset);
	unlock_notifier_list(&oset);
	pop_cleanup_lock(&notifier_lock);
	close(nf->psfd[0]);
	close(nf->psfd[1]);
	pthread_mutex_destroy(&nf->lock);
}

int notifier_signal(struct notifier *nf)
{
	int fd, ret, kick = 1;
	char c = 0;

	ret = read_lock_nocancel(&nf->lock);
	if (ret)
		return ret;

	fd = nf->psfd[1];

	if (!nf->notified)
		nf->notified = 1;
	else
		kick = 0;

	read_unlock(&nf->lock);

	/*
	 * XXX: we must release the lock before we write to the pipe,
	 * since we may be immediately preempted by the notification
	 * signal in case we notify the current thread.
	 */
	if (kick)
		ret = write(fd, &c, 1);

	return 0;
}

int notifier_release(struct notifier *nf)
{
	int fd, ret, kick = 1;
	char c = 1;

	/*
	 * The notifier struct is associated to the caller thread, so
	 * if the caller is cancelled, the notification struct becomes
	 * useless. We may only take a read lock here.
	 */
	ret = read_lock_nocancel(&nf->lock);
	if (ret)
		return ret;

	fd = nf->pwfd[1];

	if (nf->notified)
		nf->notified = 0;
	else
		kick = 0;

	read_unlock(&nf->lock);

	if (kick)
		ret = write(fd, &c, 1);

	return 0;
}

int notifier_wait(const struct notifier *nf) /* sighandler context */
{
	int ret;
	char c;

	ret = read(nf->pwfd[0], &c, 1);
	assert(ret == 1);

	return 0;
}

void notifier_pkg_init(void)
{
	pthread_mutexattr_t mattr;
	struct sigaction sa;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutex_init(&notifier_lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
	/*
	 * XXX: We have four requirements here:
	 *
	 * - we have to rely on Linux signals for notification, which
	 * guarantees that the target thread will get the message as
	 * soon as possible, regardless of what it was doing when
	 * notified (syscall wait, pure runtime etc.).
	 *
	 * - we must process the notifier callback fully on behalf of
	 * the target thread, since client code may rely on this
	 * assumption.  E.g. offloading the callback code to some
	 * server thread kicked from the signal handler would be a bad
	 * idea in that sense.
	 *
	 * - a notifier callback should be allowed to block using a
	 * dedicated service, until a release notification is
	 * sent. Since the callback runs over a signal handler, we
	 * have to implement this feature only using async-safe
	 * services.
	 *
	 * - we must allow a single thread to listen to multiple
	 * notification events, and the destination of each event
	 * should be easily identified.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = &notifier_sighandler;
	sa.sa_flags = SA_SIGINFO|SA_RESTART;
	sigaction(NOTIFYSIG, &sa, &notifier_old_sa);
}
