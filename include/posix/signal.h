#ifndef _XENO_POSIX_SIGNAL_H
#define _XENO_POSIX_SIGNAL_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/signal.h>
#endif /* !__KERNEL__ */

#if 1
#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember
#undef sigaction
#undef sigqueue
#undef SIGRTMIN
#undef SIGRTMAX
#endif

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#endif /* __XENO_SIM__ */

#ifdef __KERNEL__
/* These are not defined in kernel-space headers. */
#define sa_sigaction sa_handler
typedef void (*sighandler_t) (int sig);
typedef unsigned long sig_atomic_t;
#define DELAYTIMER_MAX UINT_MAX
#endif /* ! __KERNEL__ */

#define sigaction(sig, action, old) pse51_sigaction(sig, action, old)
#define sigemptyset pse51_sigemptyset
#define sigfillset pse51_sigfillset
#define sigaddset pse51_sigaddset
#define sigdelset pse51_sigdelset
#define sigismember pse51_sigismember
#define sigqueue pse51_sigqueue

#define SIGRTMIN 33
#define SIGRTMAX 64

struct pse51_thread;

#ifdef __cplusplus
extern "C" {
#endif

int sigemptyset(sigset_t *set);

int sigfillset(sigset_t *set);

int sigaddset(sigset_t *set,
	      int signum);

int sigdelset(sigset_t *set,
	      int signum);

int sigismember(const sigset_t *set,
		int signum);

int pthread_kill(struct pse51_thread *thread,
		 int sig);

int pthread_sigmask(int how,
		    const sigset_t *set,
		    sigset_t *oset);

int sigaction(int sig,
	      const struct sigaction *action,
	      struct sigaction *old);

int sigpending(sigset_t *set);

int sigwait(const sigset_t *set,
	    int *sig);

/* Real-time signals. */
int sigwaitinfo(const sigset_t *__restrict__ set,
                siginfo_t *__restrict__ info);

int sigtimedwait(const sigset_t *__restrict__ user_set,
                 siginfo_t *__restrict__ info,
                 const struct timespec *__restrict__ timeout);

/* Depart from POSIX here, we use a thread id instead of a process id. */
int sigqueue (struct pse51_thread *thread, int sig, union sigval value);

#ifdef __cplusplus
}
#endif

#else /* __KERNEL__ || __XENO_SIM__ */

#include_next <signal.h>

#endif /* __KERNEL__ || __XENO_SIM__ */

#endif /* _XENO_POSIX_SIGNAL_H */
