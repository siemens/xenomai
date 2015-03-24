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
#include <memory.h>
#include <malloc.h>
#include <assert.h>
#include <pwd.h>
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

struct coppernode __node_info = {
	.mem_pool = 1024 * 1024, /* Default, 1Mb. */
	.no_mlock = 0,
	.no_registry = 0,
	.no_sanity = !CONFIG_XENO_SANITY,
	.silent_mode = 0,
	.registry_root = DEFAULT_REGISTRY_ROOT,
	.session_label = NULL,
	.session_root = NULL,
};

pid_t __node_id;

#ifdef CONFIG_XENO_COBALT
int __cobalt_print_bufsz = 32 * 1024;
#endif

static DEFINE_PRIVATE_LIST(skins);

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
#define regroot_opt	3
		.name = "registry-root",
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
#define affinity_opt	6
		.name = "cpu-affinity",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
#define silent_opt	7
		.name = "silent",
		.has_arg = 0,
		.flag = &__node_info.silent_mode,
		.val = 1
	},
	{
#define version_opt	8
		.name = "version",
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	},
	{
#define dumpconfig_opt	9
		.name = "dump-config",
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	},
	{
#define no_sanity_opt	10
		.name = "no-sanity",
		.has_arg = 0,
		.flag = &__node_info.no_sanity,
		.val = 1
	},
	{
#define sanity_opt	11
		.name = "sanity",
		.has_arg = 0,
		.flag = &__node_info.no_sanity,
		.val = 0
	},
	{
		.name = NULL,
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	}
};

static inline void print_version(void)
{
	extern const char *xenomai_version_string;
	fprintf(stderr, "%s\n", xenomai_version_string);
}

static inline void dump_configuration(void)
{
	int n;

	for (n = 0; config_strings[n]; n++)
		puts(config_strings[n]);
}

static void usage(void)
{
	struct copperskin *skin;

	print_version();
        fprintf(stderr, "usage: program <options>, where options may be:\n");
	fprintf(stderr, "--mem-pool-size=<sizeK>          size of the main heap (kbytes)\n");
        fprintf(stderr, "--no-mlock                       do not lock memory at init (Mercury only)\n");
        fprintf(stderr, "--registry-root=<path>           root path of registry\n");
        fprintf(stderr, "--no-registry                    suppress object registration\n");
        fprintf(stderr, "--session=<label>                label of shared multi-processing session\n");
        fprintf(stderr, "--cpu-affinity=<cpu[,cpu]...>    set CPU affinity of threads\n");
        fprintf(stderr, "--[no-]sanity                    disable/enable sanity checks\n");
        fprintf(stderr, "--silent                         tame down verbosity\n");
        fprintf(stderr, "--version                        get version information\n");
        fprintf(stderr, "--dump-config                    dump configuration settings\n");

	if (pvlist_empty(&skins))
		return;

	pvlist_for_each_entry(skin, &skins, __reserved.next) {
		if (skin->help)
			skin->help();
	}
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
	ret = sched_setaffinity(0, sizeof(__node_info.cpu_affinity),
				&__node_info.cpu_affinity);
	if (ret) {
		warning("invalid CPU in affinity list '%s'", cpu_list);
		return __bt(-errno);
	}

	return 0;
}

static inline char **prep_args(int argc, char *const argv[], int *largcp)
{
	int in, out, n, maybe_arg, lim;
	char **uargv, *p;

	uargv = malloc(argc * sizeof(char *));
	if (uargv == NULL)
		return NULL;

	for (n = 0; n < argc; n++) {
		uargv[n] = strdup(argv[n]);
		if (uargv[n] == NULL)
			return NULL;
	}

	lim = argc;
	in = maybe_arg = 0;
	while (in < lim) {
		if ((uargv[in][0] == '-' && uargv[in][1] != '-') ||
		    (maybe_arg && uargv[in][0] != '-')) {
			p = strdup(uargv[in]);
			for (n = in, out = in + 1; out < argc; out++, n++) {
				free(uargv[n]);
				uargv[n] = strdup(uargv[out]);
			}
			free(uargv[argc - 1]);
			uargv[argc - 1] = p;
			if (*p == '-')
				maybe_arg = 1;
			lim--;
		} else {
			in++;
			maybe_arg = 0;
		}
	}

	*largcp = lim;

	return uargv;
}

static inline void pack_args(int *argcp, int *largcp, char **argv)
{
	int in, out;

	for (in = out = 0; in < *argcp; in++) {
		if (*argv[in])
			argv[out++] = argv[in];
		else {
			free(argv[in]);
			(*largcp)--;
		}
	}

	*argcp = out;
}

static struct option *build_option_array(int *base_opt_startp)
{
	struct option *options, *q;
	struct copperskin *skin;
	const struct option *p;
	int nopts;

	nopts = sizeof(base_options) / sizeof(base_options[0]);

	if (!pvlist_empty(&skins)) {
		pvlist_for_each_entry(skin, &skins, __reserved.next) {
			p = skin->options;
			if (p) {
				while (p->name) {
					nopts++;
					p++;
				}
			}
		}
	}

	options = malloc(sizeof(*options) * nopts);
	if (options == NULL)
		return NULL;

	q = options;

	if (!pvlist_empty(&skins)) {
		pvlist_for_each_entry(skin, &skins, __reserved.next) {
			p = skin->options;
			if (p) {
				skin->__reserved.opt_start = q - options;
				while (p->name)
					memcpy(q++, p++, sizeof(*q));
			}
			skin->__reserved.opt_end = q - options;
		}
	}

	*base_opt_startp = q - options;
	memcpy(q, base_options, sizeof(base_options));

	return options;
}

static int parse_base_options(int *argcp, char *const **argvp,
			      int *largcp, char ***uargvp,
			      const struct option *options,
			      int base_opt_start)
{
	int c, lindex, ret, n;
	char **uargv;

	/*
	 * Prepare a user argument vector we can modify, copying the
	 * one we have been given by the application code in
	 * copperplate_init(). This vector will be expunged from
	 * Xenomai proper options as we discover them.
	 */
	uargv = prep_args(*argcp, *argvp, largcp);
	if (uargv == NULL)
		return -ENOMEM;

	*uargvp = uargv;
	opterr = 0;

	/*
	 * NOTE: since we pack the argument vector on the fly while
	 * processing the options, optarg should be considered as
	 * volatile by skin option handlers; i.e. strdup() is required
	 * if the value has to be retained. Values from the user
	 * vector returned by copperplate_init() live in permanent
	 * memory though.
	 */

	for (;;) {
		lindex = -1;
		c = getopt_long(*largcp, uargv, "", options, &lindex);
		if (c == EOF)
			break;
		if (lindex == -1)
			continue;

		switch (lindex - base_opt_start) {
		case mempool_opt:
			__node_info.mem_pool = atoi(optarg) * 1024;
			break;
		case session_opt:
			__node_info.session_label = strdup(optarg);
			break;
		case regroot_opt:
			__node_info.registry_root = strdup(optarg);
			break;
		case affinity_opt:
			ret = collect_cpu_affinity(optarg);
			if (ret)
				return ret;
			break;
		case no_mlock_opt:
		case no_sanity_opt:
		case no_registry_opt:
		case sanity_opt:
		case silent_opt:
			break;
		case version_opt:
			print_version();
			exit(0);
		case dumpconfig_opt:
			dump_configuration();
			exit(0);
		case help_opt:
			usage();
			exit(0);
		default:
			/* Skin option, don't process yet. */
			continue;
		}

		/*
		 * Clear the first byte of the base option we found
		 * (including any companion argument), pack_args()
		 * will expunge all options we have already handled.
		 *
		 * NOTE: this code relies on the fact that only long
		 * options with double-dash markers can be parsed here
		 * after prep_args() did its job (we do not support
		 * -foo as a long option). This is aimed at reserving
		 * use of short options to the application layer,
		 * sharing only the long option namespace with the
		 * Xenomai core libs.
		 */
		n = optind - 1;
		if (uargv[n][0] != '-' || uargv[n][1] != '-')
			/* Clear the separate argument value. */
			uargv[n--][0] = '\0';
		uargv[n][0] = '\0'; /* Clear the option switch. */
	}

	pack_args(argcp, largcp, uargv);

	optind = 0;

	return 0;
}

static int parse_skin_options(int *argcp, int largc, char **uargv,
			      const struct option *options)
{
	struct copperskin *skin;
	int lindex, n, c, ret;

	for (;;) {
		lindex = -1;
		c = getopt_long(largc, uargv, "", options, &lindex);
		if (c == EOF)
			break;
		if (lindex == -1)
			continue; /* Not handled here. */
		pvlist_for_each_entry(skin, &skins, __reserved.next) {
			if (skin->parse_option == NULL)
				continue;
			if (lindex < skin->__reserved.opt_start ||
			    lindex >= skin->__reserved.opt_end)
				continue;
			lindex -= skin->__reserved.opt_start;
			ret = skin->parse_option(lindex, optarg);
			if (ret == 0)
				break;
			return ret;
		}
		n = optind - 1;
		if (uargv[n][0] != '-' || uargv[n][1] != '-')
			/* Clear the separate argument value. */
			uargv[n--][0] = '\0';
		uargv[n][0] = '\0'; /* Clear the option switch. */
	}

	pack_args(argcp, &largc, uargv);

	optind = 0;

	return 0;
}

/*
 * Routine to bring up the basic copperplate features, but not enough
 * to run over a non-POSIX real-time interface though. For internal
 * code only, such as sysregd. No code traversed should depend on
 * __node_info.
 */
void copperplate_bootstrap_minimal(const char *arg0, char *mountpt,
				   int regflags)
{
	int ret;

	boilerplate_init();

	__node_id = get_thread_pid();

	ret = debug_init();
	if (ret) {
		warning("failed to initialize debugging features");
		goto fail;
	}

	ret = heapobj_pkg_init_private();
	if (ret) {
		warning("failed to initialize main private heap");
		goto fail;
	}

	ret = __registry_pkg_init(arg0, mountpt, regflags);
	if (ret)
		goto fail;

	return;
fail:
	early_panic("initialization failed, %s", symerror(ret));
}

static int get_session_root(int *regflags_r)
{
	char *sessdir, *session;
	struct passwd *pw;
	int ret;

	pw = getpwuid(geteuid());
	if (pw == NULL)
		return -errno;

	if (__node_info.session_label == NULL) {
		ret = asprintf(&session, "anon@%d", __node_id);
		if (ret < 0)
			return -ENOMEM;
		__node_info.session_label = session;
		*regflags_r |= REGISTRY_ANON;
	}

	ret = asprintf(&sessdir, "%s/%s/%s",
		       __node_info.registry_root,
		       pw->pw_name, __node_info.session_label);
	if (ret < 0)
		return -ENOMEM;

	__node_info.session_root = sessdir;

	return 0;
}

/* The application-level copperplate init call. */

void copperplate_init(int *argcp, char *const **argvp)
{
	int ret, largc, base_opt_start, regflags = 0;
	struct copperskin *skin;
	struct option *options;
	static int init_done;
	char **uargv = NULL;
	struct service svc;

	if (init_done) {
		warning("duplicate call to %s() ignored", __func__);
		warning("(xeno-config --no-auto-init disables implicit call)");
		return;
	}

	boilerplate_init();

	threadobj_init_key();

	/* Our node id. is the tid of the main thread. */
	__node_id = get_thread_pid();

	/* No ifs, no buts: we must be called over the main thread. */
	assert(getpid() == __node_id);

	/* Define default CPU affinity, i.e. no particular affinity. */
	CPU_ZERO(&__node_info.cpu_affinity);

	/*
	 * Build the global option array, merging the base and
	 * per-skin option sets.
	 */
	options = build_option_array(&base_opt_start);
	if (options == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	/*
	 * Parse the base options first, to bootstrap the core with
	 * the right config values.
	 */
	ret = parse_base_options(argcp, argvp, &largc, &uargv,
				 options, base_opt_start);
	if (ret)
		goto fail;

#ifndef CONFIG_SMP
	if (__node_info.no_sanity == 0) {
		ret = get_static_cpu_count();
		if (ret > 0)
			early_panic("running non-SMP libraries on SMP kernel?\n"
	    "              build with --enable-smp or disable check with --no-sanity");
	}
#endif

	ret = debug_init();
	if (ret) {
		warning("failed to initialize debugging features");
		goto fail;
	}

	ret = heapobj_pkg_init_private();
	if (ret) {
		warning("failed to initialize main private heap");
		goto fail;
	}

	/*
	 * We need the session label to be known before we create the
	 * shared heap, which is named after the former.
	 */
	ret = get_session_root(&regflags);
	if (ret)
		goto fail;

	ret = heapobj_pkg_init_shared();
	if (ret) {
		warning("failed to initialize main shared heap");
		goto fail;
	}

	if (__node_info.no_registry == 0) {
		ret = registry_pkg_init(uargv[0], regflags);
		if (ret)
			goto fail;
	}

	ret = threadobj_pkg_init();
	if (ret) {
		warning("failed to initialize multi-threading package");
		goto fail;
	}

	ret = timerobj_pkg_init();
	if (ret) {
		warning("failed to initialize timer support");
		goto fail;
	}

#ifdef CONFIG_XENO_MERCURY
	if (__node_info.no_mlock == 0) {
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret) {
			ret = -errno;
			warning("failed to lock memory");
			goto fail;
		}
	}
#endif

	/*
	 * Now that we have bootstrapped the core, we may call the
	 * skin handlers for parsing their own options, which in turn
	 * may create system objects on the fly.
	 */
	if (!pvlist_empty(&skins)) {
		ret = parse_skin_options(argcp, largc, uargv, options);
		if (ret)
			goto fail;

		CANCEL_DEFER(svc);

		pvlist_for_each_entry(skin, &skins, __reserved.next) {
			ret = skin->init();
			if (ret)
				break;
		}

		CANCEL_RESTORE(svc);

		if (ret) {
			warning("skin %s won't initialize", skin->name);
			goto fail;
		}
	}

	free(options);

#ifdef CONFIG_XENO_DEBUG
	if (__node_info.silent_mode == 0) {
		warning("Xenomai compiled with %s debug enabled,\n"
			"                              "
			"%shigh latencies expected [--enable-debug=%s]",
#ifdef CONFIG_XENO_DEBUG_FULL
			"full", "very ", "full"
#else
			"partial", "", "partial"
#endif
			);
	}
#endif

	/*
	 * The final user arg vector only contains options we could
	 * not handle. The caller should be able to process them, or
	 * bail out.
	 */
	*argvp = uargv;
	init_done = 1;

	return;
fail:
	early_panic("initialization failed, %s", symerror(ret));
}

void copperplate_register_skin(struct copperskin *p)
{
	pvlist_append(&p->__reserved.next, &skins);
}

/**
 * @{
 *
 * @page api-tags API service tags
 *
 * The non-POSIX API services based on the Copperplate library may be
 * restricted to particular calling contexts, or entail specific
 * side-effects. This information applies to the Alchemy API services,
 * and to all RTOS emulators as well. To describe this information,
 * each service documented by this section bears a set of tags when
 * applicable.
 *
 * The table below matches the tags used throughout the documentation
 * with the description of their meaning for the caller.
 *
 * @par
 * <b>Context tags</b>
 * <TABLE>
 * <TR><TH>Tag</TH> <TH>Context on entry</TH></TR>
 * <TR><TD>xthread-only</TD>	<TD>Must be called from a Xenomai thread</TD></TR>
 * <TR><TD>xhandler-only</TD>	<TD>Must be called from a Xenomai handler. See note.</TD></TR>
 * <TR><TD>xcontext</TD>	<TD>May be called from any Xenomai context (thread or handler).</TD></TR>
 * <TR><TD>pthread-only</TD>	<TD>Must be called from a regular POSIX thread</TD></TR>
 * <TR><TD>thread-unrestricted</TD>	<TD>May be called from a Xenomai or regular POSIX thread indifferently</TD></TR>
 * <TR><TD>xthread-nowait</TD>	<TD>May be called from a Xenomai thread unrestricted, or from a regular thread as a non-blocking service only. See note.</TD></TR>
 * <TR><TD>unrestricted</TD>	<TD>May be called from any context previously described</TD></TR>
 * </TABLE>
 *
 * @note A Xenomai handler is most often used for callback-based
 * timeout notifications. This context is @a NOT mapped to a regular
 * Linux signal handler, it is actually underlaid by a special thread
 * context, so that async-unsafe POSIX services may be invoked
 * internally by the API implementation when running on behalf of such
 * handler. Therefore, calling Xenomai API services from asynchronous
 * regular signal handlers is fundamentally unsafe.
 *
 * @note A non-blocking call for an API service is defined by a
 * special value passed as a timeout specification.
 *
 * @par
 * <b>Possible side-effects over the Cobalt core (i.e. dual kernel configuration)</b>
 * <TABLE>
 * <TR><TH>Tag</TH> <TH>Description</TH></TR>
 * <TR><TD>switch-primary</TD>		<TD>the caller may switch to primary mode</TD></TR>
 * <TR><TD>switch-secondary</TD>	<TD>the caller may switch to secondary mode</TD></TR>
 * </TABLE>
 *
 * @note As a rule of thumb, any service which might block the caller,
 * causes a switch to primary mode if invoked from secondary
 * mode. This rule might not apply in case the service can complete
 * fully from user-space without any syscall entailed, due to a
 * particular optimization (e.g. fast acquisition of semaphore
 * resources directly from user-space in the non-contended
 * case). Therefore, the switch-{primary, secondary} tags denote
 * either services which _will_ always switch the caller to the mode
 * mentioned, or _might_ have to do so, depending on the context. The
 * absence of such tag indicates that such services can complete in
 * either modes and as such will entail no switch.
 *
 * @}
 */
