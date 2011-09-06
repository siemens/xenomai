/**
 * @file
 * Analogy for Linux, test program for waveform generation
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
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

#include "wf_facilities.h"

void do_print_usage(void)
{
	fprintf(stdout, "usage:\twf_generate [OPTS]\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout, 
		"\t\t -t, --type: waveform type "
		"(sine, sawtooth, triangular, steps\n");
	fprintf(stdout, "\t\t -f, --frequency: waveform frequency\n");
	fprintf(stdout, "\t\t -a, --amplitude: waveform amplitude\n");
	fprintf(stdout, "\t\t -o, --offset: waveform offet\n");
	fprintf(stdout, "\t\t -s, --sampling-frequency: sampling frequency\n");
	fprintf(stdout, "\t\t -O, --outpout: output file (or stdout)\n");
	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

static struct option opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"type", required_argument, NULL, 't'},
	{"frequency", required_argument, NULL, 'f'},
	{"amplitude", required_argument, NULL, 'a'},
	{"offset", required_argument, NULL, 'o'},
	{"sampling-frequency", required_argument, NULL, 's'},
	{"output", required_argument, NULL, 'O'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

int select_type(struct waveform_config *config, char *arg)
{
	int err = 0;

	if (!strcmp(arg, "sine")) 
		config->wf_kind = WAVEFORM_SINE;
	else if (!strcmp(arg, "sawtooth"))
		config->wf_kind = WAVEFORM_SAWTOOTH;
	else if (!strcmp(arg, "triangular"))
		config->wf_kind = WAVEFORM_TRIANGULAR;
	else if (!strcmp(arg, "steps"))
		config->wf_kind = WAVEFORM_STEPS;
	else {
		fprintf(stderr, "Error: type %s is not recognized\n", arg);
		err = -EINVAL;
	}

	return err;
}

struct config {
	int verbose;
	char *filename;
	FILE *output;
	struct waveform_config wf;
};

void cleanup_config(struct config *cfg)
{
	if (cfg->output && strcmp(cfg->filename, "stdout")) {
		fclose(cfg->output);
	}
}

int init_config(struct config *cfg, int argc, char *argv[])
{
	int err = 0;

	memset(cfg, 0, sizeof(struct config));

	cfg->wf.wf_kind = WAVEFORM_SINE;
	cfg->wf.wf_frequency = 500.0;
	cfg->wf.wf_amplitude = 1.0;
	cfg->wf.wf_offset = 0.0;
	cfg->wf.spl_frequency = 1000.0;
	cfg->wf.spl_count = 0;

	while ((err = getopt_long(argc, 
				  argv, "vt:f:a:o:s:O:h", opts, NULL)) >= 0) {
		
		switch (err) {

		case 'v':
			cfg->verbose = 1;
			break;
		case 't':
			err = select_type(&cfg->wf, optarg);
			if (err < 0)
				goto out;
			break;
		case 'f':
			errno = 0;
			cfg->wf.wf_frequency = strtod(optarg, NULL);
			if (errno) {
				err = -errno;
				goto bad_conversion;
			}
			break;
		case 'a':
			errno = 0;
			cfg->wf.wf_amplitude = strtod(optarg, NULL);
			if (errno) {
				err = -errno;
				goto bad_conversion;
			}
			break;
		case 'o':
			errno = 0;
			cfg->wf.wf_offset = strtod(optarg, NULL);
			if (errno) {
				err = -errno;
				goto bad_conversion;
			}
			break;
		case 's':
			errno = 0;
			cfg->wf.spl_frequency = strtod(optarg, NULL);
			if (errno) {
				err = -errno;
				goto bad_conversion;
			}
			break;
		case 'O':
			cfg->filename = optarg;
			break;
		case 'h':
		default:
			err = -EINVAL;
			do_print_usage();
			goto out;
		}
	}
	
	err = 0;

	if (cfg->filename != NULL) {
		cfg->output = fopen(cfg->filename, "w");
		if (cfg->output == NULL) {
			err = -errno;
			fprintf(stderr, "%s: %s\n", cfg->filename, strerror(errno));
			goto out;
		}
	} else {
		cfg->output = stdout;
		cfg->filename = "stdout";
	}

	if (isatty(fileno(cfg->output))) {
		err = -EINVAL;
		fprintf(stderr, 
			"Error: output terminals are not allowed (%s)\n", 
			cfg->filename);
		goto out;
	}

out:
	if (err < 0)
		cleanup_config(cfg);

	return err;

bad_conversion:
	fprintf(stderr, "Error:  bad option(s) value(s)\n");
	do_print_usage();
	return err;	
}

int main(int argc, char *argv[])
{
	int err = 0;
	struct config cfg;
	double *values = NULL;
	
	err = init_config(&cfg, argc, argv);
	if (err < 0)
		goto out;

	err = a4l_wf_check_config(&cfg.wf);
	if (err < 0)
		goto out;

	a4l_wf_set_sample_count(&cfg.wf);

	if (cfg.verbose) {
		char *types[] = {"sine", "sawtooth", "triangular", "steps"};
		fprintf(stderr, "Waveform type: %s\n", types[cfg.wf.wf_kind]);
		fprintf(stderr, "Amplitude: %F\n", cfg.wf.wf_amplitude);
		fprintf(stderr, "Frequency: %F\n", cfg.wf.wf_frequency);
		fprintf(stderr, "Offset: %F\n", cfg.wf.wf_offset);
		fprintf(stderr, 
			"Sampling frequency: %F\n", cfg.wf.spl_frequency);
		fprintf(stderr, "Samples count: %d\n", cfg.wf.spl_count);
		fprintf(stderr, "Output file: %s\n", cfg.filename);
	}
	
	values = malloc(cfg.wf.spl_count * sizeof(double));
	if (!values) {
		err = -ENOMEM;
		fprintf(stderr, "Error: values allocations failed\n");
		goto out;
	}

	a4l_wf_init_values(&cfg.wf, values);

	err = fwrite(values, sizeof(double), cfg.wf.spl_count, cfg.output);
	if (err != cfg.wf.spl_count) {
		err = -errno;
		perror("Error: output file write: )");
		goto out;
	}

	if (cfg.verbose) {
		int i;

		fprintf(stderr, "Dumping values:\n");
		for (i = 0; i < cfg.wf.spl_count; i++)
			fprintf(stderr, "[%d]: %F\n", i, values[i]);
	}

out:
	cleanup_config(&cfg);

	return err;
}
