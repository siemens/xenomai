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

#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include "copperplate/init.h"
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/registry.h"

unsigned int __tick_period_arg = 1000; /* Default, in microseconds. */

unsigned int __mem_pool_arg = 128 * 1024; /* Default, in bytes. */

int __no_mlock_arg = 0; /* Default, do mlockall. */

char __registry_mountpt_arg[PATH_MAX];

int __no_registry_arg = 0; /* Default, registry on when compiled in. */

const char *__session_label_arg = "anon";

int __reset_session_arg = 0;

static int mkdir_mountpt = 1;

static const struct option base_options[] = {
	{
#define help_opt	0
		.name = "help",
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	},
	{
#define tickarg_opt	1
		.name = "tick-period",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define mempool_opt	2
		.name = "mem-pool-size",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define no_mlock_opt	3
		.name = "no-mlock",
		.has_arg = 0,
		.flag = &__no_mlock_arg,
		.val = 1
	},
	{
#define mountpt_opt	4
		.name = "registry-mountpt",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define no_registry_opt	5
		.name = "no-registry",
		.has_arg = 0,
		.flag = &__no_registry_arg,
		.val = 1
	},
	{
#define session_opt	6
		.name = "session",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define reset_session_opt	7
		.name = "reset-session",
		.has_arg = 0,
		.flag = &__reset_session_arg,
		.val = 1
	},
	{
		.name = NULL,
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	}
};

static void usage(void)
{
	fprintf(stderr, "usage: program <options>, where options may be:\n");
	fprintf(stderr, "--tick-period=<period>		base period of the RTOS clock (us)\n");
	fprintf(stderr, "--mem-pool-size=<sizeK>	size of the main heap (kbytes)\n");
	fprintf(stderr, "--no-mlock			do not lock memory at init\n");
	fprintf(stderr, "--registry-mountpt=<path>	mount point of registry\n");
	fprintf(stderr, "--no-registry			suppress object registration\n");
	fprintf(stderr, "--session=<label>		label of shared multi-processing session\n");
	fprintf(stderr, "--reset			remove any older session\n");
}

static void do_cleanup(void)
{
	if (!__no_registry_arg)
		registry_pkg_destroy();
}

int copperplate_init(int argc, char *const argv[])
{
	int c, lindex, ret;

	/* Set a reasonable default value for the registry mount point. */
	sprintf(__registry_mountpt_arg, "/mnt/xenomai/%d", getpid());

	for (;;) {
		c = getopt_long_only(argc, argv, "", base_options, &lindex);
		if (c == EOF)
			break;
		if (c == '?') {
			usage();
			return -EINVAL;
		}
		if (c > 0)
			continue;

		switch (lindex) {
		case tickarg_opt:
			__tick_period_arg = atoi(optarg);
			break;
		case mempool_opt:
			__mem_pool_arg = atoi(optarg) * 1024;
			break;
		case mountpt_opt:
			strncpy(__registry_mountpt_arg, optarg, PATH_MAX - 1);
			__registry_mountpt_arg[PATH_MAX - 1] = '\0';
			mkdir_mountpt = 0;
#ifndef CONFIG_XENO_REGISTRY
			warning("Xenomai compiled without registry support");
#endif
			break;
		case session_opt:
			__session_label_arg = optarg;
#ifndef CONFIG_XENO_PSHARED
			warning("Xenomai compiled without shared multi-processing support");
#endif
			break;
		case no_mlock_opt:
		case no_registry_opt:
		case reset_session_opt:
			break;
		case help_opt:
			usage();
			exit(0);
		default:
			return -EINVAL;
		}
	}

	ret = heapobj_pkg_init_private();
	if (ret) {
		warning("failed to initialize main private heap");
		return ret;
	}

	ret = heapobj_pkg_init_shared();
	if (ret) {
		warning("failed to initialize main shared heap");
		return ret;
	}

	if (!__no_registry_arg) {
		ret = registry_pkg_init(argv[0], __registry_mountpt_arg,
					mkdir_mountpt);
		if (ret)
			return ret;
	}

	atexit(do_cleanup);
	threadobj_pkg_init();

	if (!__no_mlock_arg) {
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret) {
			warning("failed to lock memory");
			return -errno;
		}
	}

	return 0;
}
