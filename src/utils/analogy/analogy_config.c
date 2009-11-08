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

/* Declare precompilation constants */
#define __NBMIN_ARG 2
#define __NBMAX_ARG 3
#define __OPTS_DELIMITER ","

/* Declare prog variables */
int vlevel = 1;
int unatt_act = 0;
struct option a4l_conf_opts[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"quiet", no_argument, NULL, 'q'},
	{"version", no_argument, NULL, 'V'},
	{"remove", no_argument, NULL, 'r'},
	{"read-buffer-size", required_argument, NULL, 'R'},
	{"write-buffer-size", required_argument, NULL, 'W'},
	{0},
};

int compute_opts(char *opts, unsigned int *nb, unsigned long *res)
{

	int ret = 0, len, ofs;

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
				ret = -errno;
				goto out_compute_opts;
			}				
		}
		opts += ofs + 1;
	} while (len != ofs);

out_compute_opts:
	(*nb) *= sizeof(unsigned long);
	return ret;
}

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
	fprintf(stdout, "\t\t -r, --remove: remove configured driver\n");
	fprintf(stdout,
		"\t\t -R, --read-buffer-size: read buffer size in kB\n");
	fprintf(stdout,
		"\t\t -W, --write-buffer-size: write buffer size in kB\n");
}

int main(int argc, char *argv[])
{
	int c;
	char *devfile;
	a4l_lnkdesc_t lnkdsc;
	int chk_nb, ret = 0, fd = -1;

	/* Init the descriptor structure */
	memset(&lnkdsc, 0, sizeof(a4l_lnkdesc_t));

	/* Compute arguments */
	while ((c =
		getopt_long(argc, argv, "hvqVrR:W:", a4l_conf_opts,
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
			unatt_act = 1;
			break;
		case 'R':
			break;
		case 'W':
			break;
		}
	}

	/* Check the last arguments */
	chk_nb = (unatt_act == 0) ? __NBMIN_ARG : __NBMIN_ARG - 1;
	if (argc - optind < chk_nb) {
		do_print_usage();
		goto out_a4l_config;
	}

	/* Get the device file name */
	devfile = argv[optind];

	/* Fill the descriptor with the driver name */
	if (unatt_act == 0) {
		lnkdsc.bname = argv[optind + 1];
		lnkdsc.bname_size = strlen(argv[optind + 1]);
	}

	/* Handle the last arguments: the driver-specific args */
	if (unatt_act == 1 || argc - optind != __NBMAX_ARG)
		lnkdsc.opts_size = 0;
	else {
		char *opts = argv[optind + __NBMAX_ARG - 1];
		if ((ret = compute_opts(opts, &lnkdsc.opts_size, NULL)) < 0) {
			fprintf(stderr,
				"analogy_config: specific-driver options recovery failed\n");
			fprintf(stderr,
				"\twarning: specific-driver options must be integer value\n");
			do_print_usage();
			goto out_a4l_config;
		}

		lnkdsc.opts = malloc(lnkdsc.opts_size);
		if (lnkdsc.opts == NULL) {
			fprintf(stderr,
				"analogy_config: memory allocation failed\n");
			ret = -ENOMEM;
			goto out_a4l_config;
		}

		if ((ret =
		     compute_opts(opts, &lnkdsc.opts_size, lnkdsc.opts)) < 0) {
			fprintf(stderr,
				"analogy_config: specific-driver options recovery failed\n");
			fprintf(stderr,
				"\twarning: specific-driver options must be integer value\n");
			do_print_usage();
			goto out_a4l_config;
		}
	}

	/* Open the specified file */
	fd = a4l_sys_open(devfile);
	if (fd < 0) {
		ret = fd;
		fprintf(stderr, "analogy_config: a4l_open failed ret=%d\n",
			ret);
		goto out_a4l_config;
	}

	/* Trigger the ioctl */
	if (unatt_act == 0)
		ret = a4l_sys_attach(fd, &lnkdsc);
	else
		ret = a4l_sys_detach(fd);
	if (ret < 0) {
		fprintf(stderr, "analogy_config: %s failed ret=%d\n",
			(unatt_act ==
			 0) ? "a4l_snd_attach" : "a4l_snd_detach", ret);
		goto out_a4l_config;
	}

out_a4l_config:

	if (fd >= 0)
		a4l_sys_close(fd);

	if (lnkdsc.opts != NULL)
		free(lnkdsc.opts);

	return ret;
}
