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
#include <linux/unistd.h>
#include "copperplate/init.h"
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/registry.h"
#include "copperplate/timerobj.h"

struct coppernode __this_node = {
	.mem_pool = 128 * 1024, /* Default, 128 Kb. */
	.session_label = "anon",
	.no_mlock = 0,
	.no_registry = 0,
	.reset_session = 0,
};

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
		.flag = &__this_node.no_mlock,
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
		.flag = &__this_node.no_registry,
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
		.flag = &__this_node.reset_session,
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
}

static void do_cleanup(void)
{
	if (!__this_node.no_registry)
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
			return -EINVAL;
		}
		CPU_SET(cpu, &__this_node.cpu_affinity);
		n = NULL;
	}

	free(s);

	/*
	 * Check we may use this affinity, at least one CPU from the
	 * given set should be available for running threads. Since
	 * CPU affinity will be inherited by children threads, we only
	 * have to set it here.
	 *
	 * NOTE: we don't clear __this_node.cpu_affinity on entry to this routine
	 * to allow cumulative --cpu-affinity options to appear in the
	 * command line arguments.
	 */
	ret = sched_setaffinity(0, sizeof(__this_node.cpu_affinity), &__this_node.cpu_affinity);
	if (ret) {
		warning("no valid CPU in affinity list '%s'", cpu_list);
		return -ret;
	}

	return 0;
}

pid_t copperplate_get_tid(void)
{
	return syscall(__NR_gettid);
}

#ifdef CONFIG_XENO_COBALT

static inline unsigned long get_node_id(void)
{
	/*
	 * XXX: The nucleus maintains a hash table indexed on
	 * task_pid_vnr() values for mapped shadows. This is what
	 * __NR_gettid retrieves as well.
	 */
	return copperplate_get_tid();
}

int copperplate_probe_node(unsigned int id)
{
	/*
	 * XXX: this call does NOT migrate to secondary mode therefore
	 * may be used in time-critical contexts. However, since the
	 * nucleus has to know about a probed thread to find out
	 * whether it exists, copperplate_init() must always be
	 * invoked from a real-time shadow, so that __this_node.id can
	 * be matched.
	 */
	return pthread_probe_np((pid_t)id) == 0;
}

#else /* CONFIG_XENO_MERCURY */

static inline unsigned long get_node_id(void)
{
	return getpid();
}

int copperplate_probe_node(unsigned int id)
{
	return kill((pid_t)id, 0) == 0;
}

#endif  /* CONFIG_XENO_MERCURY */

int copperplate_init(int argc, char *const argv[])
{
	int c, lindex, ret;

	/* No ifs, no buts: we must be called over the main thread. */
	assert(getpid() == copperplate_get_tid());

	__this_node.id = get_node_id();

	/* Set a reasonable default value for the registry mount point. */
	ret = asprintf(&__this_node.registry_mountpt,
		       "/mnt/xenomai/%d", getpid());
	if (ret < 0)
		return -ENOMEM;

	/* Define default CPU affinity, i.e. no particular affinity. */
	CPU_ZERO(&__this_node.cpu_affinity);
	opterr = 0;

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
		case mempool_opt:
			__this_node.mem_pool = atoi(optarg) * 1024;
			break;
		case mountpt_opt:
			__this_node.registry_mountpt = strdup(optarg);
			mkdir_mountpt = 0;
#ifndef CONFIG_XENO_REGISTRY
			warning("Xenomai compiled without registry support");
#endif
			break;
		case session_opt:
			__this_node.session_label = optarg;
#ifndef CONFIG_XENO_PSHARED
			warning("Xenomai compiled without shared multi-processing support");
#endif
			break;
		case affinity_opt:
			ret = collect_cpu_affinity(optarg);
			if (ret)
				return ret;
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

	if (!__this_node.no_registry) {
		ret = registry_pkg_init(argv[0], __this_node.registry_mountpt,
					mkdir_mountpt);
		if (ret)
			return ret;
	}

	atexit(do_cleanup);
	threadobj_pkg_init();
	ret = timerobj_pkg_init();
	if (ret) {
		warning("failed to initialize timer support");
		return ret;
	}

	if (!__this_node.no_mlock) {
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret) {
			warning("failed to lock memory");
			return -errno;
		}
	}

	/* The caller may parse its own arguments, reset the index. */
	optind = 0;

	return 0;
}
