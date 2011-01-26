/**
 * @file
 * Analogy for Linux, configuration program
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include <xeno_config.h>

#include <analogy/analogy.h>

#define __OPTS_DELIMITER ","

enum actions {
	DO_ATTACH = 0x1,
	DO_DETACH = 0x2,
	DO_BUFCONFIG = 0x4,
};

/* Declare prog variables */
int vlevel = 1;
enum actions actions = 0;
int bufsize = -1;
struct option a4l_conf_opts[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"quiet", no_argument, NULL, 'q'},
	{"version", no_argument, NULL, 'V'},
	{"remove", no_argument, NULL, 'r'},
	{"read-buffer-size", required_argument, NULL, 'R'},
	{"write-buffer-size", required_argument, NULL, 'W'},
	{"buffer-size", required_argument, NULL, 'S'},
	{0},
};

/* Misc functions */
void do_print_version(void)
{
	fprintf(stdout, "analogy_config: version %s\n", PACKAGE_VERSION);
}

void do_print_usage(void)
{
	fprintf(stdout,
		"usage:\tanalogy_config [OPTS] devfile driver "
		"<driver options, ex: 0x378,7>\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout, "\t\t -q, --quiet: quiet output\n");
	fprintf(stdout, "\t\t -V, --version: print program version\n");
	fprintf(stdout, "\t\t -r, --remove: detach a device\n");
	fprintf(stdout,
		"\t\t -S, --buffer-size: set default buffer size in kB\n");
	fprintf(stdout, "\tDeprecated options:\n");
	fprintf(stdout,
		"\t\t -R, --read-buffer-size: read buffer size in kB\n");
	fprintf(stdout,
		"\t\t -W, --write-buffer-size: write buffer size in kB\n");
}

int parse_extra_arg(char *opts, unsigned int *nb, unsigned long *res)
{

	int err = 0, len, ofs;

	/* Check arg and inits it */
	if (nb == NULL)
		return -EINVAL;
	*nb = 0;

	/* We set errno to 0 so as to be sure that
	   strtoul did not fail */
	errno = 0;

	do {
		(*nb)++;
		len = strlen(opts);
		ofs = strcspn(opts, __OPTS_DELIMITER);
		if (res != NULL) {
			res[(*nb) - 1] = strtoul(opts, NULL, 0);
			if (errno != 0) {
				err = -errno;
				goto out_compute_opts;
			}
		}
		opts += ofs + 1;
	} while (len != ofs);

out_compute_opts:
	(*nb) *= sizeof(unsigned long);
	return err;
}

int process_extra_arg(a4l_lnkdesc_t *lnkdsc, char *arg)
{
	int err = 0;

	if ((err = parse_extra_arg(arg, &lnkdsc->opts_size, NULL)) < 0) {
		goto err_opts;
	}

	lnkdsc->opts = malloc(lnkdsc->opts_size);
	if (lnkdsc->opts == NULL) {
		fprintf(stderr,
			"analogy_config: memory allocation failed\n");
		err = -ENOMEM;
		goto out;
	}

	if ((err = parse_extra_arg(arg,
				   &lnkdsc->opts_size, lnkdsc->opts)) < 0) {
		goto err_opts;
	}

out:
	return err;

err_opts:
	fprintf(stderr,
		"analogy_config: specific-driver options recovery failed\n");
	fprintf(stderr,
		"\twarning: specific-driver options must be integer value\n");
	do_print_usage();

	return err;
}

int main(int argc, char *argv[])
{
	int c;
	char *devfile;
	int err = 0, fd = -1;

	/* Compute arguments */
	while ((c =
		getopt_long(argc, argv, "hvqVrR:W:S:", a4l_conf_opts,
			    NULL)) >= 0) {
		switch (c) {
		case 'h':
			do_print_usage();
			goto out_a4l_config;
		case 'v':
			vlevel = 2;
			break;
		case 'q':
			vlevel = 0;
			break;
		case 'V':
			do_print_version();
			goto out_a4l_config;
		case 'r':
			actions |= DO_DETACH;
			break;
		case 'R':
		case 'W':
			fprintf(stdout,
				"analogy_config: the option --read-buffer-size "
				"and --write-buffer-size will be deprecated; "
				"please use --buffer-size instead (-S)\n");
		case 'S':
			actions |= DO_BUFCONFIG;
			bufsize = strtoul(optarg, NULL, 0);
			break;
		default:
			do_print_usage();
			goto out_a4l_config;
		}
	}

	/* Here we have choice:
	   - if the option -r is set, only one additional option is
	     useful
	   - if the option -S is set without no attach options
	   - if the option -S is set with attach options */

	if ((actions & DO_DETACH) && argc - optind < 1 ) {
		fprintf(stderr, "analogy_config: specify a device to detach\n");
		goto out_a4l_config;
	}

	if ((actions & DO_DETACH) && (actions & DO_BUFCONFIG)) {
		fprintf(stderr,
			"analogy_config: skipping buffer size configuration"
			"because of detach action\n");
	}

	if (!(actions & DO_DETACH) &&
	    !(actions & DO_BUFCONFIG) && argc - optind < 2) {
		do_print_usage();
		goto out_a4l_config;
	} else if (!(actions & DO_DETACH) && argc - optind >= 2)
		actions |= DO_ATTACH;

	/* Whatever the action, we need to retrieve the device path */
	devfile = argv[optind];
	/* Init the descriptor structure */

	/* Open the specified file */
	fd = a4l_sys_open(devfile);
	if (fd < 0) {
		err = fd;
		fprintf(stderr,
			"analogy_config: a4l_open failed err=%d\n", err);
		goto out_a4l_config;
	}

	if (actions & DO_DETACH) {

		err = a4l_sys_detach(fd);
		if (err < 0)
			fprintf(stderr,
				"analogy_config: detach failed err=%d\n", err);
		goto out_a4l_config;
	}

	if (actions & DO_ATTACH) {

		a4l_lnkdesc_t lnkdsc;

		memset(&lnkdsc, 0, sizeof(a4l_lnkdesc_t));

		/* Fill the descriptor with the driver name */
		lnkdsc.bname = argv[optind + 1];
		lnkdsc.bname_size = strlen(argv[optind + 1]);

		/* Process driver-specific options */
 		if (argc - optind == 3) {

			err = process_extra_arg(&lnkdsc, argv[optind + 2]);
			if (err < 0)
				goto out_a4l_config;
		}

		/* Go... */
		err = a4l_sys_attach(fd, &lnkdsc);
		if (err < 0) {
			fprintf(stderr,
				"analogy_config: attach failed err=%d\n", err);
			goto out_a4l_config;
		}

		if (lnkdsc.opts != NULL)
			free(lnkdsc.opts);
	}

	if (actions & DO_BUFCONFIG) {

		err = a4l_sys_bufcfg(fd, A4L_BUF_DEFMAGIC, bufsize);
		if (err < 0) {
			fprintf(stderr,
				"analogy_config: bufffer configuraiton failed "
				"(err=%d)\n", err);
			goto out_a4l_config;
		}
	}

out_a4l_config:

	if (fd >= 0)
		a4l_sys_close(fd);

	return err;
}
