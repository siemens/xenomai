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
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/registry.h"
#include "copperplate/timerobj.h"
#include "copperplate/debug.h"
#include "internal.h"

struct timespec __init_date;

static DEFINE_PRIVATE_LIST(skins);

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
#define mempool_opt	1
		.name = "mem-pool-size",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define no_mlock_opt	2
		.name = "no-mlock",
		.has_arg = 0,
		.flag = &__node_info.no_mlock,
		.val = 1
	},
	{
#define mountpt_opt	3
		.name = "registry-mountpt",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define no_registry_opt	4
		.name = "no-registry",
		.has_arg = 0,
		.flag = &__node_info.no_registry,
		.val = 1
	},
	{
#define session_opt	5
		.name = "session",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define reset_session_opt	6
		.name = "reset-session",
		.has_arg = 0,
		.flag = &__node_info.reset_session,
		.val = 1
	},
	{
#define affinity_opt	7
		.name = "cpu-affinity",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define silent_opt	8
		.name = "silent",
		.has_arg = 0,
		.flag = &__node_info.silent_mode,
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
	fprintf(stderr, "--mem-pool-size=<sizeK>	size of the main heap (kbytes)\n");
	fprintf(stderr, "--no-mlock			do not lock memory at init\n");
	fprintf(stderr, "--registry-mountpt=<path>	mount point of registry\n");
	fprintf(stderr, "--no-registry			suppress object registration\n");
	fprintf(stderr, "--session=<label>		label of shared multi-processing session\n");
	fprintf(stderr, "--reset			remove any older session\n");
	fprintf(stderr, "--cpu-affinity=<cpu[,cpu]...>	set CPU affinity of threads\n");
	fprintf(stderr, "--silent			tame down verbosity\n");
}

static void do_cleanup(void)
{
	if (!__node_info.no_registry)
		registry_pkg_destroy();
}

static int collect_cpu_affinity(const char *cpu_list)
{
	char *s = strdup(cpu_list), *p, *n;
	int ret, cpu;

	n = s;
	while ((p = strtok(n, ",")) != NULL) {
		cpu = atoi(p);
		if (cpu >= CPU_SETSIZE) {
			free(s);
			warning("invalid CPU number '%d'", cpu);
			return __bt(-EINVAL);
		}
		CPU_SET(cpu, &__node_info.cpu_affinity);
		n = NULL;
	}

	free(s);

	/*
	 * Check we may use this affinity, at least one CPU from the
	 * given set should be available for running threads. Since
	 * CPU affinity will be inherited by children threads, we only
	 * have to set it here.
	 *
	 * NOTE: we don't clear __node_info.cpu_affinity on entry to
	 * this routine to allow cumulative --cpu-affinity options to
	 * appear in the command line arguments.
	 */
	ret = sched_setaffinity(0, sizeof(__node_info.cpu_affinity), &__node_info.cpu_affinity);
	if (ret) {
		warning("no valid CPU in affinity list '%s'", cpu_list);
		return __bt(-ret);
	}

	return 0;
}

void copperplate_init(int argc, char *const argv[])
{
	struct copperskin *skin;
	int c, lindex, ret;

	__RT(clock_gettime(CLOCK_COPPERPLATE, &__init_date));

	/* Our node id. is the tid of the main thread. */
	__node_id = copperplate_get_tid();

	/* No ifs, no buts: we must be called over the main thread. */
	assert(getpid() == __node_id);

	/* Set a reasonable default value for the registry mount point. */
	ret = asprintf(&__node_info.registry_mountpt,
		       "/mnt/xenomai/%d", getpid());
	if (ret < 0) {
		ret = -ENOMEM;
		goto fail;
	}

	/* Define default CPU affinity, i.e. no particular affinity. */
	CPU_ZERO(&__node_info.cpu_affinity);
	opterr = 0;

	for (;;) {
		c = getopt_long_only(argc, argv, "", base_options, &lindex);
		if (c == EOF)
			break;
		if (c > 0)
			continue;
		switch (lindex) {
		case mempool_opt:
			__node_info.mem_pool = atoi(optarg) * 1024;
			break;
		case mountpt_opt:
			__node_info.registry_mountpt = strdup(optarg);
			mkdir_mountpt = 0;
#ifndef CONFIG_XENO_REGISTRY
			warning("Xenomai compiled without registry support");
#endif
			break;
		case session_opt:
			__node_info.session_label = optarg;
#ifndef CONFIG_XENO_PSHARED
			warning("Xenomai compiled without shared multi-processing support");
#endif
			break;
		case affinity_opt:
			ret = collect_cpu_affinity(optarg);
			if (ret)
				goto fail;
			break;
		case no_mlock_opt:
		case no_registry_opt:
		case reset_session_opt:
		case silent_opt:
			break;
		case help_opt:
			usage();
			exit(0);
		}
	}

	ret = debug_pkg_init();
	if (ret) {
		warning("failed to initialize debugging features");
		goto fail;
	}

	ret = heapobj_pkg_init_private();
	if (ret) {
		warning("failed to initialize main private heap");
		goto fail;
	}

	ret = heapobj_pkg_init_shared();
	if (ret) {
		warning("failed to initialize main shared heap");
		goto fail;
	}

	if (!__node_info.no_registry) {
		ret = registry_pkg_init(argv[0], __node_info.registry_mountpt,
					mkdir_mountpt);
		if (ret)
			goto fail;
	}

	atexit(do_cleanup);
	threadobj_pkg_init();
	ret = timerobj_pkg_init();
	if (ret) {
		warning("failed to initialize timer support");
		goto fail;
	}

	if (!__node_info.no_mlock) {
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret) {
			ret = -errno;
			warning("failed to lock memory");
			goto fail;
		}
	}

	if (pvlist_empty(&skins)) {
		warning("no skin detected in program");
		ret = -EINVAL;
		goto fail;
	}

	pvlist_for_each_entry(skin, &skins, next) {
		optind = 0;
		ret = skin->init(argc, argv);
		if (ret) {
			warning("skin %s won't initialize", skin->name);
			goto fail;
		}
	}

#ifdef __XENO_DEBUG__
	if (!__node_info.silent_mode) {
		warning("Xenomai compiled with %s debug enabled,\n"
			"                                     "
			"%shigh latencies expected [--enable-debug=%s]",
#ifdef __XENO_DEBUG_FULL__
			"full", "very ", "full"
#else
			"partial", "", "partial"
#endif
			);
	}
#endif
	optind = 0;

	return;
fail:
	panic("initialization failed, %s", symerror(ret));
}

void copperplate_register_skin(struct copperskin *p)
{
	pvlist_append(&p->next, &skins);
}
