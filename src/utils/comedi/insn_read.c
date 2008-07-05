/**
 * Comedi for RTDM, instruction test program
 *
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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
#include <sys/mman.h>
#include <errno.h>
#include <getopt.h>

#include <native/task.h>

#include <comedi/comedi.h>

#define FILENAME "comedi0"
#define BUF_SIZE 10000
#define SCAN_CNT 10

static unsigned char buf[BUF_SIZE];
static double dbuf[BUF_SIZE];
static char *filename;
static int verbose;
static int real_time;
static int idx_subd;
static int idx_chan;
static int idx_rng = -1;
static unsigned int scan_size = SCAN_CNT;

static RT_TASK rt_task_desc;

struct option insn_read_opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"real-time", no_argument, NULL, 'r'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"scan-count", required_argument, NULL, 'S'},
	{"channel", required_argument, NULL, 'c'},
	{"range", required_argument, NULL, 'R'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

void do_print_usage(void)
{
	fprintf(stdout, "usage:\tinsn_read [OPTS]\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout,
		"\t\t -r, --real-time: enable real-time acquisition mode\n");
	fprintf(stdout,
		"\t\t -d, --device: device filename (comedi0, comedi1, ...)\n");
	fprintf(stdout, "\t\t -s, --subdevice: subdevice index\n");
	fprintf(stdout, "\t\t -S, --scan-count: count of scan to perform\n");
	fprintf(stdout, "\t\t -c, --channel: channel to use\n");
	fprintf(stdout, "\t\t -R, --range: range to use\n");
	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

int main(int argc, char *argv[])
{
	int ret = 0, i;
	unsigned int cnt = 0;
	comedi_desc_t dsc;
	comedi_chinfo_t *chinfo;
	comedi_rnginfo_t *rnginfo;

	/* Computes arguments */
	while ((ret = getopt_long(argc,
				  argv,
				  "vrd:s:S:c:R:h", insn_read_opts,
				  NULL)) >= 0) {
		switch (ret) {
		case 'v':
			verbose = 1;
			break;
		case 'r':
			real_time = 1;
			break;
		case 'd':
			filename = optarg;
			break;
		case 's':
			idx_subd = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			scan_size = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			idx_chan = strtoul(optarg, NULL, 0);
			break;
		case 'R':
			idx_rng = strtoul(optarg, NULL, 0);
			break;
		case 'h':
		default:
			do_print_usage();
			return 0;
		}
	}

	if (real_time != 0) {

		if (verbose != 0)
			printf("insn_read: switching to real-time mode\n");

		/* Prevents any memory-swapping for this program */
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "insn_read: mlockall failed (ret=%d)\n",
				ret);
			goto out_insn_read;
		}

		/* Turns the current process into an RT task */
		ret = rt_task_shadow(&rt_task_desc, NULL, 1, 0);
		if (ret < 0) {
			fprintf(stderr,
				"insn_read: rt_task_shadow failed (ret=%d)\n",
				ret);
			goto out_insn_read;
		}

	}

	/* Opens the device */
	ret = comedi_open(&dsc, FILENAME);
	if (ret < 0) {
		fprintf(stderr, "insn_read: comedi_open %s failed (ret=%d)\n",
			FILENAME, ret);
		return ret;
	}

	/* Checks there is an input subdevice */
	if (dsc.idx_read_subd < 0) {
		ret = -ENOENT;
		fprintf(stderr, "insn_read: no input subdevice available\n");
		goto out_insn_read;
	}

	if (verbose != 0) {
		printf("insn_read: device %s opened (fd=%d)\n", filename,
		       dsc.fd);
		printf("insn_read: basic descriptor retrieved\n");
		printf("\t subdevices count = %d\n", dsc.nb_subd);
		printf("\t read subdevice index = %d\n", dsc.idx_read_subd);
		printf("\t write subdevice index = %d\n", dsc.idx_write_subd);
	}

	/* Allocates a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL) {
		ret = -ENOMEM;
		fprintf(stderr, "insn_read: info buffer allocation failed\n");
		goto out_insn_read;
	}

	/* Gets this data */
	ret = comedi_fill_desc(&dsc);
	if (ret < 0) {
		fprintf(stderr, "insn_read: comedi_fill_desc failed (ret=%d)\n",
			ret);
		goto out_insn_read;
	}

	if (verbose != 0)
		printf("insn_read: complex descriptor retrieved\n");

	if (idx_rng >= 0) {

		ret =
		    comedi_get_rnginfo(&dsc, idx_subd, idx_chan, idx_rng,
				       &rnginfo);
		if (ret < 0) {
			fprintf(stderr,
				"insn_read: failed to recover range descriptor\n");
			goto out_insn_read;
		}

		if (verbose != 0) {
			printf("insn_read: range descriptor retrieved\n");
			printf("\t min = %ld\n", rnginfo->min);
			printf("\t max = %ld\n", rnginfo->max);
		}
	}

	/* Retrieves the subdevice data size */
	ret = comedi_get_chinfo(&dsc, idx_subd, idx_chan, &chinfo);
	if (ret < 0) {
		fprintf(stderr,
			"insn_read: info for channel %d on subdevice %d not available (ret=%d)\n",
			idx_chan, idx_subd, ret);
		goto out_insn_read;
	}

	/* Sets the data size to read */
	scan_size *= chinfo->nb_bits / 8;

	if (verbose != 0) {
		printf("insn_read: channel width is %u bits\n",
		       chinfo->nb_bits);
		printf("insn_read: global scan size is %u\n", scan_size);
	}

	while (cnt < scan_size) {
		int tmp = (scan_size - cnt) < BUF_SIZE ?
		    (scan_size - cnt) : BUF_SIZE;

		/* Switches to RT primary mode */
		if (real_time != 0) {
			ret = rt_task_set_mode(0, T_PRIMARY, NULL);
			if (ret < 0) {
				fprintf(stderr,
					"insn_read: rt_task_set_mode failed (ret=%d)\n",
					ret);
				goto out_insn_read;
			}
		}

		/* Performs the synchronous read */
		ret = comedi_sync_read(&dsc,
				       idx_subd, 0, CHAN(idx_chan), buf, tmp);

		if (ret < 0)
			goto out_insn_read;

		/* If a range was selected, converts the samples */
		if (idx_rng >= 0) {
			if (comedi_to_phys(chinfo, rnginfo, dbuf, buf, ret) < 0) {
				fprintf(stderr,
					"insn_read: data conversion failed (ret=%d)\n",
					ret);
				goto out_insn_read;
			}
		}

		/* Dumps the results */
		for (i = 0; i < ret; i++) {

			/* Prints the output byte by byte */
			printf("0x%x ", buf[i]);

			/* Unlike comedi_async_read(), comedi_sync_read() cannot
			   retrieve data which are not aligned with the channel
			   width; so, it is easier to properly print data.
			 */
			if ((i + 1) % (chinfo->nb_bits / 8) == 0) {

				/* If a range was selected, prints the converted value */
				if (idx_rng >= 0)
					printf("\t-> %F",
					       dbuf[i / (chinfo->nb_bits / 8)]);

				printf("\n");
			}
		}

		/* Updates the count */
		cnt += ret;
	}

	if (verbose != 0)
		printf("insn_read: %u bytes successfully received\n", cnt);

      out_insn_read:

	/* Frees the information buffer */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Releases the file descriptor */
	comedi_close(&dsc);

	return ret;
}
