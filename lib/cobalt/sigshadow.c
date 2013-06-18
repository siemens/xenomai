#include <pthread.h>
#include <signal.h>
#include <asm/xenomai/syscall.h>

#ifdef __UCLIBC__
static inline int backtrace(void **buffer, int size)
{
	/*
	 * We have no backtrace support in uClibc.
	 */
	return 0;
}
#else /* !__UCLIBC__ */
#include <execinfo.h>
#endif /* !__UCLIBC__ */

static struct sigaction sigshadow_action_orig;

/*
 * The following handler is part of the inner user-interface: should
 * remain extern.
 */
int cobalt_sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	void *frames[SIGSHADOW_BACKTRACE_DEPTH];
	int action, arg, nr, skip;

	if (si->si_code != SI_QUEUE)
		return 0;

	action = sigshadow_action(si->si_int);

	switch (action) {
	case SIGSHADOW_ACTION_HARDEN:
		XENOMAI_SYSCALL1(sc_nucleus_migrate, XENOMAI_XENO_DOMAIN);
		break;
	case SIGSHADOW_ACTION_BACKTRACE:
		arg = sigshadow_arg(si->si_int);
		nr = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
		/* Skip the sighandler context. */
		skip = nr > 3 ? 3 : 0;
		XENOMAI_SYSCALL3(sc_nucleus_backtrace, nr - skip, frames + skip, arg);
		break;
	default:
		return 0;
	}

	return 1;
}

static void sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	const struct sigaction *const sa = &sigshadow_action_orig;
	sigset_t saved_sigset;

	if (cobalt_sigshadow_handler(sig, si, ctxt))
		return;

	/* Not a signal sent by the Xenomai nucleus */
	if (((sa->sa_flags & SA_SIGINFO) == 0 && sa->sa_handler == NULL)
	    || ((sa->sa_flags & SA_SIGINFO) && sa->sa_sigaction == NULL))
		return;

	pthread_sigmask(SIG_SETMASK, &sa->sa_mask, &saved_sigset);

	if (!(sa->sa_flags & SA_SIGINFO))
		sa->sa_handler(sig);
	else
		sa->sa_sigaction(sig, si, ctxt);

	pthread_sigmask(SIG_SETMASK, &saved_sigset, NULL);
}

static void install_sigshadow(void)
{
	struct sigaction new_sigshadow_action;
	sigset_t saved_sigset;
	sigset_t mask_sigset;

	sigemptyset(&mask_sigset);
	sigaddset(&mask_sigset, SIGSHADOW);

	new_sigshadow_action.sa_flags = SA_SIGINFO | SA_RESTART;
	new_sigshadow_action.sa_sigaction = sigshadow_handler;
	sigemptyset(&new_sigshadow_action.sa_mask);
	pthread_sigmask(SIG_BLOCK, &mask_sigset, &saved_sigset);

	sigaction(SIGSHADOW,
		  &new_sigshadow_action, &sigshadow_action_orig);

	if ((sigshadow_action_orig.sa_flags & SA_NODEFER) == 0)
		sigaddset(&sigshadow_action_orig.sa_mask, SIGSHADOW);

	pthread_sigmask(SIG_SETMASK, &saved_sigset, NULL);
}

void cobalt_sigshadow_install_once(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, install_sigshadow);
}
