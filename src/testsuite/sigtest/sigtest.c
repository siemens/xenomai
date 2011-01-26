#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#ifndef __UCLIBC__
#include <execinfo.h>
#endif /* !__UCLIBC__ */

#include <asm-generic/bits/sigshadow.h>
#include <testing/sigtest_syscall.h>
#include <asm/xenomai/bits/bind.h>

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(array[0]))

static int shifted_muxid;

int sigtest_queue(int *retvals, size_t nr)
{
	return XENOMAI_SKINCALL2(shifted_muxid,
				 __NR_sigtest_queue, retvals, nr);
}

int sigtest_wait_pri(void)
{
	return XENOMAI_SKINCALL0(shifted_muxid, __NR_sigtest_wait_pri);
}

int sigtest_wait_sec(void)
{
	return XENOMAI_SKINCALL0(shifted_muxid, __NR_sigtest_wait_sec);
}

static xnsighandler *mysh;

void sigtest_handler(union xnsiginfo *gen_si)
{
	mysh(gen_si);
}

__attribute__((constructor)) void __init_sigtest_interface(void)
{
	int muxid;

	muxid = xeno_bind_skin(SIGTEST_SKIN_MAGIC, "SIGTEST", "xeno_sigtest", sigtest_handler);

	shifted_muxid = __xn_mux_shifted_id(muxid);
}

static volatile unsigned seen;
static int cascade_res;

void mark_seen(union xnsiginfo *gen_si)
{
	struct sigtest_siginfo *si = (struct sigtest_siginfo *)gen_si;
	seen |= (1 << si->sig_nr);
}

void mark_seen_2(int sig)
{
	seen |= 2;
}

void mark_seen_2_bt(int sig)
{
#ifndef __UCLIBC__
	void *buf[200];
	int nelems = backtrace(buf, sizeof(buf)/sizeof(buf[0]));
	fputs("\n>>>>>>>>>>>>>>>>>>>>> Please "
	      "check that the following backtrace looks correct:\n", stderr);
	backtrace_symbols_fd(buf, nelems, 2);
	fputs("<<<<<<<<<<<<<<<<<<<<< End of backtrace\n\n", stderr);
#endif /* !__UCLIBC__ */
	seen |= 2;
}

void cascade_pri(union xnsiginfo *gen_si __attribute__((unused)))
{
	cascade_res = sigtest_wait_pri() == -EINTR ? -EINTR : cascade_res;
}

void cascade_sec(union xnsiginfo *gen_si __attribute__((unused)))
{
	cascade_res = sigtest_wait_sec() == -EINTR ? -EINTR : cascade_res;
}

static unsigned failed, success;

#define test_assert(expr)					\
	({							\
		if (expr) {					\
			++success;				\
			fprintf(stderr, #expr ": success.\n");	\
		} else {					\
			++failed;				\
			fprintf(stderr, #expr " failed\n");	\
		}						\
	})

#define check(expr, expected)						\
	({								\
		int rc = (expr);					\
		if (rc == (expected)) {					\
			++success;					\
			fprintf(stderr, #expr ": success.\n");		\
		} else {						\
			++failed;					\
			fprintf(stderr, #expr " failed: %d\n", -rc);	\
		}							\
	})

struct cond {
	pthread_mutex_t mx;
	pthread_cond_t cnd;
	int val;
};

void *dual_signals(void *cookie)
{
	struct cond *c = (struct cond *)cookie;
	int one_restart[] = { -ERESTART, };

	pthread_set_name_np(pthread_self(), "dual_signals");

	check(sigtest_queue(one_restart, ARRAY_SIZE(one_restart)), 0);
	pthread_mutex_lock(&c->mx);
	c->val = 1;
	pthread_cond_signal(&c->cnd);
	while (c->val != 2)
		check(pthread_cond_wait(&c->cnd, &c->mx), 0);
	c->val = 3;
	pthread_cond_signal(&c->cnd);
	pthread_mutex_unlock(&c->mx);
	pthread_exit(NULL);
}

void *dual_signals2(void *cookie)
{
	int one_restart[] = { -ERESTART, };

	pthread_set_name_np(pthread_self(), "dual_signals");
	check(sigtest_queue(one_restart, ARRAY_SIZE(one_restart)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(seen == 3);
	pthread_exit(NULL);
}

int main(void)
{
	struct sched_param sparam = { .sched_priority = 1 };

	mlockall(MCL_CURRENT | MCL_FUTURE);

	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sparam);

	int one_restart[] = { -ERESTART, };
	mysh = mark_seen;
	seen = 0;
	check(sigtest_queue(one_restart, ARRAY_SIZE(one_restart)), 0);
	check(sigtest_wait_pri(), 0);
	test_assert(seen == 1);

	seen = 0;
	check(sigtest_queue(one_restart, ARRAY_SIZE(one_restart)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(seen == 1);

	int one_intr[] = { -EINTR, };
	seen = 0;
	check(sigtest_queue(one_intr, ARRAY_SIZE(one_intr)), 0);
	check(sigtest_wait_pri(), -EINTR);
	test_assert(seen == 1);

	seen = 0;
	check(sigtest_queue(one_intr, ARRAY_SIZE(one_intr)), 0);
	check(sigtest_wait_sec(), 0); /* Signal does not interrupt
				       * secondary-mode syscall */
	test_assert(seen == 1);

	int sixteen_restart[] = { [0 ... 15] = -ERESTART, };
	seen = 0;
	check(sigtest_queue(sixteen_restart, ARRAY_SIZE(sixteen_restart)), 0);
	check(sigtest_wait_pri(), 0);
	test_assert(seen == ((1 << 16) - 1));

	seen = 0;
	check(sigtest_queue(sixteen_restart, ARRAY_SIZE(sixteen_restart)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(seen == ((1 << 16) - 1));

	int middle_intr[] = { [0 ... 7] = -ERESTART, [8] = -EINTR, [9 ... 15] = -ERESTART,
	};
	seen = 0;
	check(sigtest_queue(middle_intr, ARRAY_SIZE(middle_intr)), 0);
	check(sigtest_wait_pri(), -EINTR);
	test_assert(seen == ((1 << 16) - 1));

	seen = 0;
	check(sigtest_queue(middle_intr, ARRAY_SIZE(middle_intr)), 0);
	check(sigtest_wait_sec(), 0); /* Signal does not interrupt
				       * secondary-mode syscall */
	test_assert(seen == ((1 << 16) - 1));

	int seventeen_restart[] = { [0 ... 16] = -ERESTART };
	mysh = cascade_pri;
	cascade_res = ~0;
	check(sigtest_queue(seventeen_restart, ARRAY_SIZE(seventeen_restart)), 0);
	check(sigtest_wait_pri(), 0);
	test_assert(cascade_res == ~0);

	cascade_res = ~0;
	check(sigtest_queue(seventeen_restart, ARRAY_SIZE(seventeen_restart)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(cascade_res == ~0);

	int seventeen_intr[] = { [0 ... 15] = -ERESTART, [16] = -EINTR };
	cascade_res = ~0;
	check(sigtest_queue(seventeen_intr, ARRAY_SIZE(seventeen_intr)), 0);
	check(sigtest_wait_pri(), 0);
	test_assert(cascade_res == -EINTR);

	cascade_res = ~0;
	check(sigtest_queue(seventeen_intr, ARRAY_SIZE(seventeen_intr)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(cascade_res == -EINTR);

	/* Cascade secondary mode call. */
	mysh = cascade_sec;
	cascade_res = ~0;
	check(sigtest_queue(seventeen_restart, ARRAY_SIZE(seventeen_restart)), 0);
	check(sigtest_wait_pri(), 0);
	test_assert(cascade_res == ~0);

	cascade_res = ~0;
	check(sigtest_queue(seventeen_restart, ARRAY_SIZE(seventeen_restart)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(cascade_res == ~0);

	cascade_res = ~0;
	check(sigtest_queue(seventeen_intr, ARRAY_SIZE(seventeen_intr)), 0);
	check(sigtest_wait_pri(), 0);
	test_assert(cascade_res == ~0);

	cascade_res = ~0;
	check(sigtest_queue(seventeen_intr, ARRAY_SIZE(seventeen_intr)), 0);
	check(sigtest_wait_sec(), 0);
	test_assert(cascade_res == ~0);

	/* Try and mix linux signals and xeno signals (this test does
	   not work as expected, but turns out to be a good test for
	   pthread_cond_wait and signals, so, keep it). */
	struct timespec ts;
	pthread_t tid;
	struct cond c;
	mysh = mark_seen;
	seen = 0;
	pthread_mutex_init(&c.mx, NULL);
	pthread_cond_init(&c.cnd, NULL);
	c.val = 0;
	signal(SIGUSR1, mark_seen_2);
	pthread_create(&tid, NULL, dual_signals, &c);
	check(pthread_mutex_lock(&c.mx), 0);
	while (c.val != 1)
		check(pthread_cond_wait(&c.cnd, &c.mx), 0);
	ts.tv_sec = 0;
	ts.tv_nsec = 20000000;
	nanosleep(&ts, NULL);
	c.val = 2;
	/* thread received the xeno signals, now send the linux signal */
	pthread_kill(tid, SIGUSR1);
	pthread_cond_signal(&c.cnd); /* Now, wake-up. */
	while (c.val != 3)
		check(pthread_cond_wait(&c.cnd, &c.mx), 0);
	pthread_mutex_unlock(&c.mx);
	test_assert(seen == 3);
	pthread_join(tid, NULL);

	/* Try and mix linux signals and xeno signals. Take 2. */
	signal(SIGUSR1, mark_seen_2_bt);
	seen = 0;
	pthread_create(&tid, NULL, dual_signals2, NULL);
	ts.tv_sec = 0;
	ts.tv_nsec = 15000000;
	nanosleep(&ts, NULL);
	pthread_kill(tid, SIGUSR1);
	pthread_join(tid, NULL);

	fprintf(stderr, "Failed %u/%u\n", failed, success + failed);
	sleep(1);
	exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
}
