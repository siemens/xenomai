#ifndef _XENO_ASM_GENERIC_BITS_SIGSHADOW_H
#define _XENO_ASM_GENERIC_BITS_SIGSHADOW_H

#include <pthread.h>
#include <signal.h>

pthread_once_t __attribute__((weak))
	xeno_sigshadow_installed = PTHREAD_ONCE_INIT;
struct sigaction __attribute__((weak)) xeno_saved_sigshadow_action;

int __attribute__((weak))
xeno_sigwinch_handler(int sig, siginfo_t *si, void *ctxt)
{
	int action;

	if (si->si_code != SI_QUEUE)
		return 0;

	action = sigshadow_action(si->si_int);

	switch(action) {
	case SIGSHADOW_ACTION_HARDEN:
		XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_XENO_DOMAIN);
		break;

	case SIGSHADOW_ACTION_RENICE: {
		struct sched_param param;
		int policy;

		param.sched_priority = sigshadow_arg(si->si_int);
		policy = param.sched_priority > 0 ? SCHED_FIFO: SCHED_OTHER;
		pthread_setschedparam(pthread_self(), policy, &param);
		break;
	}

	default:
		return 0;
	}

	return 1;
}

void __attribute__((weak))
xeno_sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	const struct sigaction *const sa = &xeno_saved_sigshadow_action;
	sigset_t saved_sigset;

	if (xeno_sigwinch_handler(sig, si, ctxt))
		return;

	/* Not a signal sent by Xenomai nucleus */
	if ((!(sa->sa_flags & SA_SIGINFO) && !sa->sa_handler)
	    || ((sa->sa_flags & SA_SIGINFO) && !sa->sa_sigaction))
		return;

	pthread_sigmask(SIG_SETMASK, &sa->sa_mask, &saved_sigset);
	if (!(sa->sa_flags & SA_SIGINFO))
		sa->sa_handler(sig);
	else
		sa->sa_sigaction(sig, si, ctxt);
	pthread_sigmask(SIG_SETMASK, &saved_sigset, NULL);
	return;
}

void __attribute__((weak)) xeno_sigshadow_install(void)
{
	struct sigaction new_sigshadow_action;

	new_sigshadow_action.sa_flags = SA_SIGINFO | SA_RESTART;
	new_sigshadow_action.sa_sigaction = xeno_sigshadow_handler;
	sigemptyset(&new_sigshadow_action.sa_mask);

	sigaction(SIGSHADOW,
		  &new_sigshadow_action, &xeno_saved_sigshadow_action);
	if (!(xeno_saved_sigshadow_action.sa_flags & SA_NODEFER))
		sigaddset(&xeno_saved_sigshadow_action.sa_mask, SIGSHADOW);
}

static inline void sigshadow_install_once(void)
{
	pthread_once(&xeno_sigshadow_installed, xeno_sigshadow_install);
}
#endif /* _XENO_ASM_GENERIC_BITS_SIGSHADOW_H */
