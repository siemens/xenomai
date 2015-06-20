/*
 * fork->exec test.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <xeno_config.h>
#include <boilerplate/libc.h>
#include <smokey/smokey.h>

smokey_test_plugin(posix_fork,
		   SMOKEY_NOARGS,
		   "Check POSIX fork->exec sequence."
);

#ifdef HAVE_FORK
#define do_fork fork
#else
#define do_fork vfork
#endif

/*
 * The purpose of this test is to check whether Cobalt detects and
 * handles a fork->exec sequence properly for Xenomai-enabled threads,
 * with respect to managing their respective shadow contexts. Cobalt
 * should drop the child's shadow upon detecting exec(), then create
 * another one for the emerging process's main() thread as usual.
 *
 * We don't have to do much beyond firing such sequence fo testing: if
 * Cobalt messes up, the kernel will certainly crash.
 */
static int run_posix_fork(struct smokey_test *t, int argc, char *const argv[])
{
	struct timespec req;

	switch (do_fork()) {
	case -1:
		error(1, errno, "fork/vfork");
	case 0:
		/*
		 * Re-exec ourselves without running any test, this is
		 * enough for creating a shadow context.
		 */
		execl(CONFIG_XENO_PREFIX "/bin/smokey", "smokey", NULL);
		_exit(99);
	default:
		req.tv_sec = 0;
		req.tv_nsec = 20000000;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
	}

	return 0;
}
