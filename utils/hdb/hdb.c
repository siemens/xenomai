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
#include <malloc.h>
#include <stdio.h>
#include <error.h>
#include <fcntl.h>
#include <copperplate/cluster.h>
#include <xenomai/init.h>

static const struct option options[] = {
	{
#define dump_cluster_opt	0
		.name = "dump-cluster",
		.has_arg = required_argument,
	},
	{ /* Sentinel */ }
};

void application_usage(void)
{
        fprintf(stderr, "usage: %s <option>:\n", get_program_name());
	fprintf(stderr, "--dump-cluster <name>		dump cluster <name>\n");
}

static int check_shared_heap(const char *cmd)
{
#ifndef CONFIG_XENO_PSHARED
	fprintf(stderr,
		"%s is available for dumping shared clusters\n"
		" only. However --disable-pshared was given for building\n"
		" this particular instance of the hdb program.\n", cmd);
	return -ENOTSUP;
#else
	return 0;
#endif
}

static int get_full_owner_info(pid_t pid, char *buf, size_t len)
{
	int fd, ret;
	char *path;

	ret = asprintf(&path, "/proc/%d/cmdline", pid);
	if (ret < 0)
		return -ENOMEM;

	fd = open(path, O_RDONLY);
	free(path);
	if (fd < 0)
		return -errno;

	ret = read(fd, buf, len - 1);
	close(fd);
	if (ret < 0)
		return -errno;

	buf[ret] = '\0';

	return 0;
}

static int walk_cluster(struct cluster *c, struct clusterobj *cobj)
{
	char pid[16], cmdline[50];
	int ret;

	ret = get_full_owner_info(clusterobj_cnode(cobj),
				  cmdline, sizeof(cmdline));
	if (ret)
		return ret == -ENOENT ? 0 : ret;

	snprintf(pid, sizeof(pid), "[%d]", clusterobj_cnode(cobj));
	printf("%-9s %-20s %.*s\n", pid, cmdline,
	       (int)clusterobj_keylen(cobj),
	       (const char *)clusterobj_key(cobj));
	
	return 0;
}

static int dump_cluster(const char *name)
{
	struct cluster cluster;
	int ret;

	ret = check_shared_heap("--dump-cluster");
	if (ret)
		return ret;

	ret = cluster_init(&cluster, name);
	if (ret)
		return ret;

	return cluster_walk(&cluster, walk_cluster);
}

int main(int argc, char *const argv[])
{
	const char *cluster_name = NULL;
	int lindex, c, ret = 0;

	for (;;) {
		c = getopt_long_only(argc, argv, "", options, &lindex);
		if (c == EOF)
			break;
		if (c == '?') {
			xenomai_usage();
			return EINVAL;
		}
		if (c > 0)
			continue;

		switch (lindex) {
		case dump_cluster_opt:
			cluster_name = optarg;
			break;
		default:
			return EINVAL;
		}
	}

	if (cluster_name)
		ret = dump_cluster(cluster_name);

	if (ret)
		error(1, -ret, "hdb");

	return 0;
}
