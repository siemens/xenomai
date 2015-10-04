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
#include <sys/stat.h>
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
#include <signal.h>

static const struct option base_options[] = {
	{
#define help_opt	0
		.name = "help",
		.has_arg = no_argument,
	},
#define file_opt	1
	{
		.name = "file",
		.has_arg = required_argument,
	},
#define path_opt	2
	{
		.name = "path",
		.has_arg = required_argument,
	},
#define filter_opt	3	/* Alias for filter-in */
	{
		.name = "filter",
		.has_arg = required_argument,
	},
#define filter_in_opt	4
	{
		.name = "filter-in",
		.has_arg = required_argument,
	},
#define filter_out_opt	5	/* Alias for !filter-in */
	{
		.name = "filter-out",
		.has_arg = required_argument,
	},
	{ /* Sentinel */ }
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

struct location {
	unsigned long pc;
	char *function;
	char *file;
	int lineno;
	struct location *next;	/* next in mapping. */
};

struct mapping {
	char *name;
	struct location *locs;
	struct mapping *next;
} *mapping_list = NULL;

static const struct location undefined_location; /* All zero. */

struct relax_spot {
	char *exe_path;
	char *thread_name;
	char *reason;
	pid_t pid;
	int hits;
	int depth;
	struct backtrace {
		unsigned long pc;
		struct mapping *mapping;
		const struct location *where;
	} backtrace[SIGSHADOW_BACKTRACE_DEPTH];
	struct relax_spot *next;
} *spot_list = NULL;

int spot_count, filtered_count = 0;

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

static int filter_function(struct filter *f, struct relax_spot *p)
{
	struct backtrace *b;
	int depth;

	for (depth = 0, b = p->backtrace; depth < p->depth; b++, depth++) {
		if (b->where->function &&
		    !fnmatch(f->exp, b->where->function, 0))
			return 0;
	}

	return FNM_NOMATCH;
}

static int filter_file(struct filter *f, struct relax_spot *p)
{
	struct backtrace *b;
	int depth;

	for (depth = 0, b = p->backtrace; depth < p->depth; b++, depth++) {
		if (b->where->file &&
		    !fnmatch(f->exp, b->where->file, FNM_PATHNAME))
			return 0;
	}

	return FNM_NOMATCH;
}

static int filter_map(struct filter *f, struct relax_spot *p)
{
	struct backtrace *b;
	int depth;

	for (depth = 0, b = p->backtrace; depth < p->depth; b++, depth++) {
		if (*b->mapping->name != '?' &&
		    !fnmatch(f->exp, b->mapping->name, FNM_PATHNAME))
			return 0;
	}

	return FNM_NOMATCH;
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
		ret = sscanf(filter, "%m[a-z]=%m[^\n]", &name, &f->exp);
		if (ret != 2)
			return EINVAL;
		if (strcmp(name, "thread") == 0)
			f->op = filter_thread;
		else if (strcmp(name, "pid") == 0)
			f->op = filter_pid;
		else if (strcmp(name, "exe") == 0)
			f->op = filter_exe;
		else if (strcmp(name, "function") == 0)
			f->op = filter_function;
		else if (strcmp(name, "file") == 0)
			f->op = filter_file;
		else if (strcmp(name, "map") == 0)
			f->op = filter_map;
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

	ret = fscanf(fp, "libraries: =%m[^\n]\n", &search_path);
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

static char *resolve_path(char *mapping)
{
	struct ldpath_dir *dpath;
	char *path, *basename;
	int ret;

	/*
	 * Don't use the original mapping name verbatim if
	 * CROSS_COMPILE was specified, it is unlikely that the right
	 * target file could be found at the same place on the host.
	 */
	if (*mapping == '?' ||
	    (toolchain_prefix == NULL && access(mapping, F_OK) == 0))
		return mapping;

	basename = strrchr(mapping, '/');
	if (basename++ == NULL)
		basename = mapping;

	for (dpath = ldpath_list; dpath; dpath = dpath->next) {
		ret = asprintf(&path, "%s/%s", dpath->path, basename);
		if (ret < 0)
			goto no_mem;
		/* Pick first match. */
		if (access(path, F_OK) == 0) {
			free(mapping);
			return path;
		}
		free(path);
	}

	/* No match. Leave the mapping name unchanged */
	return mapping;

no_mem:
	error(1, ENOMEM, "resolve_path failed");
	return NULL;		/* not reached. */
}

static void read_spots(FILE *fp)
{
	struct relax_spot *p;
	struct mapping *m;
	unsigned long pc;
	char *mapping, c;
	ENTRY e, *ep;
	int ret;

	ret = fscanf(fp, "%d\n", &spot_count);
	if (ret != 1) {
		if (feof(fp))
			return;
		goto bad_input;
	}

	hcreate(spot_count * SIGSHADOW_BACKTRACE_DEPTH);

	for (;;) {
		p = malloc(sizeof(*p));
		if (p == NULL)
			error(1, 0, "out of memory");

		ret = fscanf(fp, "%m[^\n]\n", &p->exe_path);
		if (ret != 1) {
			if (feof(fp))
				return;
			goto bad_input;
		}

		ret = fscanf(fp, "%d %d %m[^ ] %m[^\n]\n",
			     &p->pid, &p->hits, &p->reason, &p->thread_name);
		if (ret != 4)
			goto bad_input;

		p->depth = 0;
		for (;;) {
			if (p->depth >= SIGSHADOW_BACKTRACE_DEPTH)
				break;
			c = getc(fp);
			if (c == '.' && getc(fp) == '\n')
				break;
			ungetc(c, fp);
			ret = fscanf(fp, "%lx %m[^\n]\n", &pc, &mapping);
			if (ret != 2)
				goto bad_input;

			mapping = resolve_path(mapping);
			e.key = mapping;
			ep = hsearch(e, FIND);
			if (ep == NULL) {
				m = malloc(sizeof(*m));
				if (m == NULL)
					goto no_mem;
				m->name = mapping;
				m->locs = NULL;
				m->next = mapping_list;
				mapping_list = m;
				e.data = m;
				ep = hsearch(e, ENTER);
				if (ep == NULL)
					goto no_mem;
			} else {
				free(mapping);
				m = ep->data;
			}

			/*
			 * Move one byte backward to point to the call
			 * site, not to the next instruction. This
			 * usually works fine...
			 */
			p->backtrace[p->depth].pc = pc - 1;
			p->backtrace[p->depth].mapping = m;
			p->backtrace[p->depth].where = &undefined_location;
			p->depth++;
		}

		if (p->depth == 0)
			goto bad_input;

		p->next = spot_list;
		spot_list = p;
	}

bad_input:
	error(1, 0, "garbled trace input");
no_mem:
	error(1, ENOMEM, "read_spots failed");
}

static inline
struct location *find_location(struct location *head, unsigned long pc)
{
	struct location *l = head;

	while (l) {
		if (l->pc == pc)
			return l;
		l = l->next;
	}

	return NULL;
}

static void resolve_spots(void)
{
	char *a2l, *a2lcmd, *s, buf[BUFSIZ];
	struct relax_spot *p;
	struct backtrace *b;
	struct location *l;
	struct mapping *m;
	struct stat sbuf;
	int ret, depth;
	FILE *fp;

	/*
	 * Fill the mapping cache with one location record per
	 * distinct PC value mentioned for each mapping.  The basic
	 * idea is to exec a single addr2line instance for all PCs
	 * belonging to any given mapping, instead of one instance per
	 * call site in each and every frame. This way, we may run
	 * slackspot on low-end targets with limited CPU horsepower,
	 * without going for unreasonably long coffee breaks.
	 */
	for (p = spot_list; p; p = p->next) {
		for (depth = 0; depth < p->depth; depth++) {
			b = p->backtrace + depth;
			l = find_location(b->mapping->locs, b->pc);
			if (l) {
				/* PC found in mapping cache. */
				b->where = l;
				continue;
			}

			l = malloc(sizeof(*l));
			if (l == NULL)
				goto no_mem;

			l->pc = b->pc;
			l->function = NULL;
			l->file = NULL;
			l->lineno = 0;
			b->where = l;
			l->next = b->mapping->locs;
			b->mapping->locs = l;
		}
	}

	/*
	 * For each mapping, try resolving PC values as source
	 * locations.
	 */
	for (m = mapping_list; m; m = m->next) {
		if (*m->name == '?')
			continue;

		ret = stat(m->name, &sbuf);
		if (ret || !S_ISREG(sbuf.st_mode))
			continue;

		ret = asprintf(&a2l,
			       "%saddr2line --demangle --inlines --functions --exe=%s",
			       toolchain_prefix, m->name);
		if (ret < 0)
			goto no_mem;

		for (l = m->locs, s = a2l, a2lcmd = NULL; l; l = l->next) {
			ret = asprintf(&a2lcmd, "%s 0x%lx", s, l->pc);
			if (ret < 0)
				goto no_mem;
			free(s);
			s = a2lcmd;
		}

		fp = popen(a2lcmd, "r");
		if (fp == NULL)
			error(1, errno, "cannot run %s", a2lcmd);

		for (l = m->locs; l; l = l->next) {
			ret = fscanf(fp, "%ms\n", &l->function);
			if (ret != 1)
				goto bad_output;
			/*
			 * Don't trust fscanf range specifier, we may
			 * have colons in the pathname.
			 */
			s = fgets(buf, sizeof(buf), fp);
			if (s == NULL)
				goto bad_output;
			s = strrchr(s, ':');
			if (s == NULL)
				continue;
			*s++ = '\0';
			if (strcmp(buf, "??")) {
				l->lineno = atoi(s);
				l->file = strdup(buf);
			}
		}

		pclose(fp);
		free(a2lcmd);
	}

	return;

bad_output:
	error(1, 0, "garbled addr2line output");
no_mem:
	error(1, ENOMEM, "resolve_locations failed");
}

static inline void put_location(struct relax_spot *p, int depth)
{
	struct backtrace *b = p->backtrace + depth;
	const struct location *where = b->where;

	printf("   #%-2d 0x%.*lx ", depth, __WORDSIZE / 4, where->pc);
	if (where->function)
		printf("%s() ", where->function);
	if (where->file) {
		printf("in %s", where->file);
		if (where->lineno)
			printf(":%d", where->lineno);
	} else {
		if (where->function == NULL)
			printf("??? ");
		if (*b->mapping->name != '?')
			printf("in [%s]", b->mapping->name);
	}
	putchar('\n');
}

static void display_spots(void)
{
	struct relax_spot *p;
	int depth, hits;

	for (p = spot_list, hits = 0; p; p = p->next) {
		hits += p->hits;
		if (match_filter_list(p)) {
			filtered_count++;
			continue;
		}
		printf("\nThread[%d] \"%s\" started by %s",
		       p->pid, p->thread_name, p->exe_path);
		if (p->hits > 1)
			printf(" (%d times)", p->hits);
		printf(":\n");
		printf("Caused by: %s\n", p->reason);
		for (depth = 0; depth < p->depth; depth++)
			put_location(p, depth);
	}

	if (filtered_count)
		printf("\n(%d spots filtered out)\n",
		       filtered_count);
	if (hits < spot_count)
		printf("\nWARNING: only %d/%d spots retrieved (some were lost)\n",
		       hits, spot_count);
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
