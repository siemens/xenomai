#include <pthread.h>
#include <signal.h>

#include <asm/xenomai/syscall.h>
#include <asm-generic/xenomai/bits/sigshadow.h>

static struct sigaction xeno_saved_sigshadow_action;

int xeno_sigwinch_handler(int sig, siginfo_t *si, void *ctxt)
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

static void xeno_sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
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

void xeno_sigshadow_install(void)
{
	struct sigaction new_sigshadow_action;
	sigset_t saved_sigset;
	sigset_t mask_sigset;

	sigemptyset(&mask_sigset);
	sigaddset(&mask_sigset, SIGSHADOW);

	new_sigshadow_action.sa_flags = SA_SIGINFO | SA_RESTART;
	new_sigshadow_action.sa_sigaction = xeno_sigshadow_handler;
	sigemptyset(&new_sigshadow_action.sa_mask);

	pthread_sigmask(SIG_BLOCK, &mask_sigset, &saved_sigset);
	sigaction(SIGSHADOW,
		  &new_sigshadow_action, &xeno_saved_sigshadow_action);
	if (!(xeno_saved_sigshadow_action.sa_flags & SA_NODEFER))
		sigaddset(&xeno_saved_sigshadow_action.sa_mask, SIGSHADOW);
	pthread_sigmask(SIG_SETMASK, &saved_sigset, NULL);
}

void xeno_sigshadow_install_once(void)
{
	static pthread_once_t sigshadow_installed = PTHREAD_ONCE_INIT;
	pthread_once(&sigshadow_installed, xeno_sigshadow_install);
}
