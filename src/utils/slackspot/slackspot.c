/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * This utility parses the output of the /proc/xenomai/debug/relax
 * vfile, to get backtraces of spurious relaxes.
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <stdint.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <search.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <getopt.h>
#include <asm/xenomai/syscall.h>

static const struct option base_options[] = {
	{
#define help_opt	0
	name: "help",
	has_arg: 0,
	flag: NULL,
	val: 0
	},
#define file_opt	1
	{
	name: "file",
	has_arg: 1,
	flag: NULL,
	val: 0
	},
#define path_opt	2
	{
	name: "path",
	has_arg: 1,
	flag: NULL,
	val: 0
	},
#define filter_opt	3	/* Alias for filter-in */
	{
	name: "filter",
	has_arg: 1,
	flag: NULL,
	val: 0
	},
#define filter_in_opt	4
	{
	name: "filter-in",
	has_arg: 1,
	flag: NULL,
	val: 0
	},
#define filter_out_opt	5	/* Alias for !filter-in */
	{
	name: "filter-out",
	has_arg: 1,
	flag: NULL,
	val: 0
	},
	{
	name: NULL,
	has_arg: 0,
	flag: NULL,
	val: 0
	}
};

struct relax_spot;

struct filter {
	int (*op)(struct filter *f, struct relax_spot *p);
	char *exp;
	struct filter *next;
} *filter_list = NULL;

int filter_not = 0;

struct ldpath_dir {
	char *path;
	struct ldpath_dir *next;
} *ldpath_list = NULL;

struct relax_spot {
	char *exe_path;
	char *thread_name;
	pid_t pid;
	int hits;
	int depth;
	struct backtrace {
		unsigned long pc;
		char *mapname;
		char *function;
		char *file;
		int lineno;
	} backtrace[SIGSHADOW_BACKTRACE_DEPTH];
	struct relax_spot *next;
} *spot_list = NULL;

int spot_count;

const char *toolchain_prefix;

static int filter_thread(struct filter *f, struct relax_spot *p)
{
	return fnmatch(f->exp, p->thread_name, 0);
}

static int filter_pid(struct filter *f,  struct relax_spot *p)
{
	char pid[16];

	sprintf(pid, "%d", p->pid);

	return fnmatch(f->exp, pid, 0);
}

static int filter_exe(struct filter *f, struct relax_spot *p)
{
	return fnmatch(f->exp, p->exe_path, FNM_PATHNAME);
}

static int build_filter_list(const char *filters)
{
	char *filter, *name;
	struct filter *f;
	int ret;

	if (filters == NULL)
		return 0;

	filter = strdup(filters);
	while ((filter = strtok(filter, ",")) != NULL) {
		f = malloc(sizeof(*f));
		ret = sscanf(filter, "%a[a-z]=%a[^\n]", &name, &f->exp);
		if (ret != 2)
			return EINVAL;
		if (strcmp(name, "thread") == 0)
			f->op = filter_thread;
		else if (strcmp(name, "pid") == 0)
			f->op = filter_pid;
		else if (strcmp(name, "exe") == 0)
			f->op = filter_exe;
		else
			return EINVAL;
		f->next = filter_list;
		filter_list = f;
		filter = NULL;
	}

	return 0;
}

static inline int match_filter_list(struct relax_spot *p)
{
	struct filter *f;

	for (f = filter_list; f; f = f->next) {
		if (f->op(f, p))
			return 1 ^ filter_not;
	}

	return 0 ^ filter_not;	/* All matched. */
}

static void build_ldpath_list(const char *ldpath)
{
	char *dir, *cccmd, *search_path, *p;
	struct ldpath_dir *dpath;
	FILE *fp;
	int ret;

	if (ldpath == NULL)
		goto cross_toolchain;

	dir = strdup(ldpath);
	while ((dir = strtok(dir, ":")) != NULL) {
		dpath = malloc(sizeof(*dpath));
		dpath->path = dir;
		dpath->next = ldpath_list;
		ldpath_list = dpath;
		dir = NULL;
	}

cross_toolchain:
	if (toolchain_prefix == NULL)
		return;

	ret = asprintf(&cccmd, "%sgcc -print-search-dirs|grep '^libraries: ='",
		       toolchain_prefix);
	if (ret < 0)
		goto no_mem;

	fp = popen(cccmd, "r");
	if (fp == NULL)
		error(1, errno, "cannot run %s", cccmd);
	free(cccmd);

	ret = fscanf(fp, "libraries: =%a[^\n]\n", &search_path);
	if (ret != 1)
		goto bad_output;

	/*
	 * Feed our ldpath list with the cross-compiler's search list
	 * for libraries.
	 */
	dir = search_path;
	while ((dir = strtok(dir, ":")) != NULL) {
		p = strrchr(dir, '/');
		if (*p)
			*p = '\0';
		dpath = malloc(sizeof(*dpath));
		dpath->path = dir;
		dpath->next = ldpath_list;
		ldpath_list = dpath;
		dir = NULL;
	}

	pclose(fp);

	return;
no_mem:
	error(1, ENOMEM, "build_ldpath_list failed");

bad_output:
	error(1, 0, "garbled gcc output for -print-search-dirs");
}

static char *resolve_path(char *mapname)
{
	struct ldpath_dir *dpath;
	char *path, *basename;
	int ret;

	/*
	 * Don't use the original map name verbatim if CROSS_COMPILE
	 * was specified, it is unlikely that the right target file
	 * could be found at the same place on the host.
	 */
	if (*mapname == '?' ||
	    (toolchain_prefix == NULL && access(mapname, F_OK) == 0))
		return mapname;

	basename = strrchr(mapname, '/');
	if (basename++ == NULL)
		basename = mapname;

	for (dpath = ldpath_list; dpath; dpath = dpath->next) {
		ret = asprintf(&path, "%s/%s", dpath->path, basename);
		if (ret < 0)
			goto no_mem;
		/* Pick first match. */
		if (access(path, F_OK) == 0) {
			free(mapname);
			return path;
		}
		free(path);
	}

	/*
	 * No match. Leave the mapname unchanged, addr2line will
	 * complain rightfully.
	 */
	return mapname;

no_mem:
	error(1, ENOMEM, "resolve_path failed");
	return NULL;		/* not reached. */
}

static void read_spots(FILE *fp)
{
	struct relax_spot *p;
	unsigned long pc;
	char *mapname, c;
	int ret;

	ret = fscanf(fp, "%d\n", &spot_count);
	if (ret != 1)
		goto bad_input;

	for (;;) {
		p = malloc(sizeof(*p));
		if (p == NULL)
			error(1, 0, "out of memory");
			
		ret = fscanf(fp, "%a[^\n]\n", &p->exe_path);
		if (ret != 1) {
			if (feof(fp))
				return;
			goto bad_input;
		}

		ret = fscanf(fp, "%d %d %a[^\n]\n",
			     &p->pid, &p->hits, &p->thread_name);
		if (ret != 3)
			goto bad_input;

		p->depth = 0;
		for (;;) {
			if (p->depth >= SIGSHADOW_BACKTRACE_DEPTH)
				break;
			c = getc(fp);
			if (c == '.' && getc(fp) == '\n')
				break;
			ungetc(c, fp);
			ret = fscanf(fp, "%lx %a[^\n]\n", &pc, &mapname);
			if (ret != 2)
				goto bad_input;
			/*
			 * Move one byte backward to point to the call
			 * site, not to the next instruction. This
			 * usually works fine...
			 */
			p->backtrace[p->depth].pc = pc - 1;
			p->backtrace[p->depth].mapname = resolve_path(mapname);
			p->depth++;
		}

		if (p->depth == 0)
			goto bad_input;

		p->next = spot_list;
		spot_list = p;
	}

bad_input:
	error(1, 0, "garbled trace input");
}

static inline void put_location(struct relax_spot *p, int depth)
{
	struct backtrace *b = p->backtrace + depth;

	printf("   #%-2d 0x%.*lx ", depth, __WORDSIZE / 8, b->pc);
	if (b->function)
		printf("%s() ", b->function);
	if (b->file) {
		printf("in %s:", b->file);
		if (b->lineno)
			printf("%d\n", b->lineno);
		else
			printf("?\n");
	} else {
		if (*b->mapname == '?')
			printf("???\n");
		else
			printf("??? [%s]\n", b->mapname);
	}
}

static void display_spots(void)
{
	struct relax_spot *p;
	int depth, hits;

	for (p = spot_list, hits = 0; p; p = p->next) {
		hits += p->hits;
		if (match_filter_list(p))
			continue;
		printf("\nThread[%d] \"%s\" started by %s",
		       p->pid, p->thread_name, p->exe_path);
		if (p->hits > 1)
			printf(" (%d times)", p->hits);
		printf(":\n");
		for (depth = 0; depth < p->depth; depth++)
			put_location(p, depth);
	}

	if (hits < spot_count)
		printf("\nWARNING: only %d/%d hits reported (some were lost)\n",
		       hits, spot_count);
}

static void resolve_spots(void)
{
	char *a2l, *a2lcmd, *s, buf[BUFSIZ];
	struct relax_spot *p;
	int ret, depth;
	FILE *fp;

	ret = asprintf(&a2l, "%saddr2line", toolchain_prefix);
	if (ret < 0)
		goto no_mem;

	for (p = spot_list; p; p = p->next) {
		for (depth = 0; depth < p->depth; depth++) {
			p->backtrace[depth].function = NULL;
			p->backtrace[depth].file = NULL;
			p->backtrace[depth].lineno = 0;

			if (*p->backtrace[depth].mapname == '?' ||
			    access(p->backtrace[depth].mapname, F_OK))
				continue;

			ret = asprintf(&a2lcmd,
				       "%s --demangle --inlines --functions --exe=%s 0x%lx",
				       a2l, p->backtrace[depth].mapname,
				       p->backtrace[depth].pc);
			if (ret < 0)
				goto no_mem;
			fp = popen(a2lcmd, "r");
			if (fp == NULL)
				error(1, errno, "cannot run %s", a2lcmd);

			ret = fscanf(fp, "%as\n", &p->backtrace[depth].function);
			if (ret != 1)
				goto next;
			/*
			 * Don't trust fscanf range specifier, we may
			 * have colons in the pathname.
			 */
			s = fgets(buf, sizeof(buf), fp);
			if (s == NULL)
				goto next;
			s = strrchr(s, ':');
			if (s == NULL)
				goto next;
			*s++ = '\0';
			p->backtrace[depth].lineno = atoi(s);
			p->backtrace[depth].file = strdup(buf);
		next:
			free(a2lcmd);
			pclose(fp);
		}
	}

	free(a2l);

	return;

no_mem:
	error(1, ENOMEM, "collect_ldd failed");
}

static void usage(void)
{
	fprintf(stderr, "usage: slackspot [CROSS_COMPILE=<toolchain-prefix>] [options]\n");
	fprintf(stderr, "   --file <file>				use trace file\n");
	fprintf(stderr, "   --path <dir[:dir...]>			set search path for exec files\n");
	fprintf(stderr, "   --filter-in <name=exp[,name...]>		exclude non-matching spots\n");
	fprintf(stderr, "   --filter <name=exp[,name...]>		alias for --filter-in\n");
	fprintf(stderr, "   --filter-out <name=exp[,name...]>		exclude matching spots\n");
	fprintf(stderr, "   --help					print this help\n");
}

int main(int argc, char *const argv[])
{
	const char *trace_file, *filters;
	const char *ldpath;
	int c, lindex, ret;
	FILE *fp;

	trace_file = NULL;
	ldpath = NULL;
	filters = NULL;
	toolchain_prefix = getenv("CROSS_COMPILE");
	if (toolchain_prefix == NULL)
		toolchain_prefix = "";

	for (;;) {
		c = getopt_long_only(argc, argv, "", base_options, &lindex);
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
		case file_opt:
			trace_file = optarg;
			break;
		case path_opt:
			ldpath = optarg;
			break;
		case filter_out_opt:
			filter_not = 1;
		case filter_in_opt:
		case filter_opt:
			filters = optarg;
			break;
		default:
			return EINVAL;
		}
	}

	fp = stdin;
	if (trace_file == NULL) {
		if (isatty(fileno(stdin))) {
			trace_file = "/proc/xenomai/debug/relax";
			goto open;
		}
	} else if (strcmp(trace_file, "-")) {
	open:
		fp = fopen(trace_file, "r");
		if (fp == NULL)
			error(1, errno, "cannot open trace file %s",
			      trace_file);
	}

	ret = build_filter_list(filters);
	if (ret)
		error(1, 0, "bad filter expression: %s", filters);

	build_ldpath_list(ldpath);
	read_spots(fp);

	if (spot_list == NULL) {
		fputs("no slacker\n", stderr);
		return 0;	/* This is not an error. */
	}

	resolve_spots();
	display_spots();

	return 0;
}
