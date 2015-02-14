/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <xeno_config.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <error.h>
#include <sys/cobalt.h>

int __cobalt_no_shadow = 1;

int __cobalt_control_bind = 1;

static int action;

static const struct option options[] = {
	{
#define status_opt	0	/* Set this first, default action. */
		.name = "status",
		.flag = &action,
		.val = status_opt,
	},
	{
#define stop_opt	1
		.name = "stop",
		.flag = &action,
		.val = stop_opt,
		.has_arg = 2,
	},
	{
#define start_opt	2
		.name = "start",
		.flag = &action,
		.val = start_opt,
	},
	{
#define help_opt	3
		.name = "help",
	},
	{ /* Sentinel */ }
};

static void usage(void)
{
	fprintf(stderr, "usage: corectl [options]:\n");
	fprintf(stderr, "   --stop [<grace-seconds>]	stop Xenomai/cobalt services\n");
	fprintf(stderr, "   --start			start Xenomai/cobalt services\n");
	fprintf(stderr, "   --status			query Xenomai/cobalt status\n");
	fprintf(stderr, "   --help			print this help\n\n");
}

static int core_stop(int grace_period)
{
	return cobalt_corectl(_CC_COBALT_STOP_CORE,
			      &grace_period, sizeof(&grace_period));
}

static int core_start(void)
{
	return cobalt_corectl(_CC_COBALT_START_CORE, NULL, 0);
}

static int core_status(void)
{
	enum cobalt_run_states state = COBALT_STATE_DISABLED;
	int ret;

	ret = cobalt_corectl(_CC_COBALT_GET_CORE_STATUS,
			     &state, sizeof(state));
	if (ret && ret != -ENOSYS)
		return ret;

	switch (state) {
	case COBALT_STATE_RUNNING:
		printf("running\n");
		break;
	case COBALT_STATE_STOPPED:
		printf("stopped\n");
		break;
	case COBALT_STATE_DISABLED:
		printf("disabled\n");
		break;
	case COBALT_STATE_WARMUP:
		printf("warmup\n");
		break;
	case COBALT_STATE_TEARDOWN:
		printf("teardown\n");
		break;
	}

	return 0;
}

int main(int argc, char *const argv[])
{
	int lindex, c, grace_period = 0, ret;
	
	for (;;) {
		c = getopt_long_only(argc, argv, "", options, &lindex);
		if (c == EOF)
			break;
		if (c == '?') {
			usage();
			return EINVAL;
		}
		if (c > 0)
			continue;

		switch (lindex) {
		case help_opt:
			usage();
			exit(0);
		case stop_opt:
			grace_period = optarg ? atoi(optarg) : 0;
		case start_opt:
		case status_opt:
			break;
		default:
			return EINVAL;
		}
	}

	switch (action) {
	case stop_opt:
		ret = core_stop(grace_period);
		break;
	case start_opt:
		ret = core_start();
		break;
	case status_opt:
		ret = core_status();
		break;
	default:
		usage();
		exit(0);
	}

	if (ret)
		error(1, -ret, "'%s' request failed", options[action].name);

	return 0;
}
