/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fnmatch.h>
#include <boilerplate/list.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/init.h>
#include "copperplate/internal.h"
#include <smokey/smokey.h>

/**
 * @defgroup smokey Smokey API
 *
 * A simple infrastructure for writing and running smoke tests.
 */

DEFINE_PRIVATE_LIST(smokey_test_list);

int smokey_keep_going;

static DEFINE_PRIVATE_LIST(register_list);

static int test_count;

static int do_list;

static const struct option smokey_options[] = {
	{
#define keep_going_opt	0
		.name = "keep-going",
		.flag = &smokey_keep_going,
		.val = 1,
	},
	{
#define run_opt		1
		.name = "run",
		.has_arg = 2,
	},
	{
#define list_opt	2
		.name = "list",
		.flag = &do_list,
		.val = 1,
	},
	{
		.name = NULL,
	}
};

static void smokey_help(void)
{
        fprintf(stderr, "--keep-going                      don't stop upon test error\n");
	fprintf(stderr, "--list                            list all tests\n");
	fprintf(stderr, "--run[=<id[,id...]>]]             run [portion of] test list\n");
}

static inline void pick_test_range(int start, int end)
{
	struct smokey_test *t, *tmp;

	/* Pick tests in the suggested range order. */

	if (start <= end) {
		pvlist_for_each_entry_safe(t, tmp, &register_list, __reserved.next) {
			if (t->__reserved.id >= start &&
			    t->__reserved.id <= end) {
				pvlist_remove(&t->__reserved.next);
				pvlist_append(&t->__reserved.next, &smokey_test_list);
			}
		}
	} else {
		pvlist_for_each_entry_reverse_safe(t, tmp, &register_list, __reserved.next) {
			if (t->__reserved.id >= end &&
			    t->__reserved.id <= start) {
				pvlist_remove(&t->__reserved.next);
				pvlist_append(&t->__reserved.next, &smokey_test_list);
			}
		}
	} 
}

static int resolve_id(const char *s)
{
	struct smokey_test *t;

	if (isdigit(*s))
		return atoi(s);

	/*
	 * CAUTION: as we transfer items from register_list to
	 * smokey_test_list, we may end up with an empty source list,
	 * which is a perfectly valid situation. Unlike having an
	 * empty registration list at startup, which would mean that
	 * no test is available from the current program.
	 */
	if (pvlist_empty(&register_list))
		return -1;

	pvlist_for_each_entry(t, &register_list, __reserved.next)
		if (!fnmatch(s, t->name, FNM_PATHNAME))
			return t->__reserved.id;

	return -1;
}

static int build_test_list(const char *test_enum)
{
	char *s = strdup(test_enum), *n, *range, *range_p, *id, *id_r;
	int start, end;

	n = s;
	while ((range = strtok_r(n, ",", &range_p)) != NULL) {
		if (*range == '\0')
			continue;
		end = -1;
		if (range[strlen(range)-1] == '-')
			end = test_count - 1;
		id = strtok_r(range, "-", &id_r);
		if (id) {
			start = resolve_id(id);
			if (*range == '-') {
				end = start;
				start = 0;
			}
			id = strtok_r(NULL, "-", &id_r);
			if (id)
				end = resolve_id(id);
			else if (end < 0)
				end = start;
			if (start < 0 || start >= test_count ||
			    end < 0 || end >= test_count)
				goto fail;
		} else {
			start = 0;
			end = test_count - 1;
		}
		pick_test_range(start, end);
		n = NULL;
	}

	free(s);

	return 0;
fail:
	warning("invalid test range in %s (each id. should be within [0-%d])",
		test_enum, test_count - 1);
	free(s);

	return -EINVAL;
}

static void list_all_tests(void)
{
	struct smokey_test *t;

	if (pvlist_empty(&register_list))
		return;

	pvlist_for_each_entry(t, &register_list, __reserved.next)
		printf("#%-3d %s\n\t%s\n",
		       t->__reserved.id, t->name, t->description);
}

static int smokey_parse_option(int optnum, const char *optarg)
{
	int ret = 0;

	switch (optnum) {
	case keep_going_opt:
		break;
	case run_opt:
		if (pvlist_empty(&register_list)) {
			warning("no test registered");
			return -EINVAL;
		}
		if (optarg)
			ret = build_test_list(optarg);
		else
			pick_test_range(0, test_count);
		if (pvlist_empty(&smokey_test_list)) {
			warning("no test selected");
			return -EINVAL;
		}
		break;
	case list_opt:
		list_all_tests();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int smokey_init(void)
{
	if (pvlist_empty(&smokey_test_list))
		copperplate_set_silent();

	return 0;
}

static struct copperskin smokey_interface = {
	.name = "smokey",
	.init = smokey_init,
	.options = smokey_options,
	.parse_option = smokey_parse_option,
	.help = smokey_help,
};

static  __attribute__ ((constructor(__SMOKEYPLUG_CTOR_PRIO+1)))
void register_smokey(void)
{
	copperplate_register_skin(&smokey_interface);
}

void smokey_register_plugin(struct smokey_test *t)
{
	pvlist_append(&t->__reserved.next, &register_list);
	t->__reserved.id = test_count++;
}
