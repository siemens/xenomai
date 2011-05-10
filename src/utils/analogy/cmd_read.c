/**
 * @file
 * Analogy for Linux, input command test program
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
#include <sys/mman.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

#include <native/task.h>

#include <analogy/analogy.h>

/* Default command's parameters */

/* For read operation, we consider
   the default subdevice index is 0 */
#define ID_SUBD 0
/* For simplicity sake, a maximum channel
   count is defined */
#define MAX_NB_CHAN 32
/* One hundred triggered scans by default */
#define NB_SCAN 100

#define FILENAME "analogy0"

#define BUF_SIZE 10000

static unsigned char buf[BUF_SIZE];
static char *filename = FILENAME;
static char *str_chans = "0,1,2,3";
static unsigned int chans[MAX_NB_CHAN];
static int verbose = 0;
static int real_time = 0;
static int use_mmap = 0;
static unsigned long wake_count = 0;

static RT_TASK rt_task_desc;

/* The command to send by default */
a4l_cmd_t cmd = {
	.idx_subd = ID_SUBD,
	.flags = 0,
	.start_src = TRIG_NOW,
	.start_arg = 0,
	.scan_begin_src = TRIG_TIMER,
	.scan_begin_arg = 8000000,	/* in ns */
	.convert_src = TRIG_TIMER,
	.convert_arg = 500000,	/* in ns */
	.scan_end_src = TRIG_COUNT,
	.scan_end_arg = 0,
	.stop_src = TRIG_COUNT,
	.stop_arg = NB_SCAN,
	.nb_chan = 0,
	.chan_descs = chans,
};

struct option cmd_read_opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"real-time", no_argument, NULL, 'r'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"scan-count", required_argument, NULL, 'S'},
	{"channels", required_argument, NULL, 'c'},
	{"mmap", no_argument, NULL, 'm'},
	{"raw", no_argument, NULL, 'w'},
	{"wake-count", required_argument, NULL, 'k'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

void do_print_usage(void)
{
	fprintf(stdout, "usage:\tcmd_read [OPTS]\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout,
		"\t\t -r, --real-time: enable real-time acquisition mode\n");
	fprintf(stdout,
		"\t\t -d, --device: device filename (analogy0, analogy1, ...)\n");
	fprintf(stdout, "\t\t -s, --subdevice: subdevice index\n");
	fprintf(stdout, "\t\t -S, --scan-count: count of scan to perform\n");
	fprintf(stdout, "\t\t -c, --channels: channels to use (ex.: -c 0,1)\n");
	fprintf(stdout, "\t\t -m, --mmap: mmap the buffer\n");
	fprintf(stdout, "\t\t -w, --raw: dump data in raw format\n");
	fprintf(stdout, 
		"\t\t -k, --wake-count: "
		"space available before waking up the process\n");
	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

int dump_raw(a4l_desc_t *dsc, a4l_cmd_t *cmd, unsigned char *buf, int size)
{
	return fwrite(buf, size, 1, stdout);
}

int dump_text(a4l_desc_t *dsc, a4l_cmd_t *cmd, unsigned char *buf, int size)
{
	static int cur_chan;

	int i, err = 0, tmp_size = 0;
	char *fmts[MAX_NB_CHAN];
	a4l_chinfo_t *chans[MAX_NB_CHAN];

	for (i = 0; i < cmd->nb_chan; i++) {
		int width;

		err = a4l_get_chinfo(dsc,
				     cmd->idx_subd,
				     cmd->chan_descs[i], &chans[i]);
		if (err < 0) {
			fprintf(stderr,
				"cmd_read: a4l_get_chinfo failed (ret=%d)\n",
				err);
			goto out;
		}

		width = a4l_sizeof_chan(chans[i]);
		if (width < 0) {
			fprintf(stderr,
				"cmd_read: incoherent info for channel %d\n",
				cmd->chan_descs[i]);
			err = width;
			goto out;
		}

		switch(width) {
		case 1:
			fmts[i] = "0x%02x ";
			break;
		case 2:
			fmts[i] = "0x%04x ";
			break;
		case 4:
		default:
			fmts[i] = "0x%08x ";
			break;
		}
	}

	while (tmp_size < size) {
		unsigned long value;

		err = a4l_rawtoul(chans[cur_chan], &value, buf + tmp_size, 1);
		if (err < 0)
			goto out;

		fprintf(stdout, fmts[cur_chan], value);

		/* We assume a4l_sizeof_chan() cannot return because
		   we already called it on the very same channel
		   descriptor */
		tmp_size += a4l_sizeof_chan(chans[cur_chan]);

		if(++cur_chan == cmd->nb_chan) {
			fprintf(stdout, "\n");
			cur_chan = 0;
		}
	}

	fflush(stdout);

out:
	return err;
}

int main(int argc, char *argv[])
{
	int ret = 0, len, ofs;
	unsigned int i, scan_size = 0, cnt = 0;
	unsigned long buf_size;
	void *map = NULL;
	a4l_desc_t dsc = { .sbdata = NULL };

	int (*dump_function) (a4l_desc_t *, a4l_cmd_t*, unsigned char *, int) =
		dump_text;

	/* Compute arguments */
	while ((ret = getopt_long(argc,
				  argv,
				  "vrd:s:S:c:mwk:h", 
				  cmd_read_opts, NULL)) >= 0) {
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
			cmd.idx_subd = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			cmd.stop_arg = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			str_chans = optarg;
			break;
		case 'm':
			use_mmap = 1;
			break;
		case 'w':
			dump_function = dump_raw;
			break;
		case 'k':
			wake_count = strtoul(optarg, NULL, 0);
			break;
		case 'h':
		default:
			do_print_usage();
			return 0;
		}
	}

	if (isatty(STDOUT_FILENO) && dump_function == dump_raw) {
		fprintf(stderr,
			"cmd_read: cannot dump raw data on a terminal\n\n");
		return -EINVAL;
	}

	/* Recover the channels to compute */
	do {
		cmd.nb_chan++;
		len = strlen(str_chans);
		ofs = strcspn(str_chans, ",");
		if (sscanf(str_chans, "%u", &chans[cmd.nb_chan - 1]) == 0) {
			fprintf(stderr, "cmd_read: bad channel argument\n");
			return -EINVAL;
		}
		str_chans += ofs + 1;
	} while (len != ofs);

	/* Update the command structure */
	cmd.scan_end_arg = cmd.nb_chan;
	cmd.stop_src = cmd.stop_arg != 0 ? TRIG_COUNT : TRIG_NONE;

	if (real_time != 0) {

		if (verbose != 0)
			printf("cmd_read: switching to real-time mode\n");

		/* Prevent any memory-swapping for this program */
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "cmd_read: mlockall failed (ret=%d)\n",
				ret);
			goto out_main;
		}

		/* Turn the current process into an RT task */
		ret = rt_task_shadow(&rt_task_desc, NULL, 1, 0);
		if (ret < 0) {
			fprintf(stderr,
				"cmd_read: rt_task_shadow failed (ret=%d)\n",
				ret);
			goto out_main;
		}
	}

	/* Open the device */
	ret = a4l_open(&dsc, filename);
	if (ret < 0) {
		fprintf(stderr, "cmd_read: a4l_open %s failed (ret=%d)\n",
			filename, ret);
		return ret;
	}

	if (verbose != 0) {
		printf("cmd_read: device %s opened (fd=%d)\n",
		       filename, dsc.fd);
		printf("cmd_read: basic descriptor retrieved\n");
		printf("\t subdevices count = %d\n", dsc.nb_subd);
		printf("\t read subdevice index = %d\n", dsc.idx_read_subd);
		printf("\t write subdevice index = %d\n", dsc.idx_write_subd);
	}

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL) {
		fprintf(stderr, "cmd_read: malloc failed \n");
		return -ENOMEM;
	}

	/* Get this data */
	ret = a4l_fill_desc(&dsc);
	if (ret < 0) {
		fprintf(stderr,
			"cmd_read: a4l_fill_desc failed (ret=%d)\n", ret);
		goto out_main;
	}

	if (verbose != 0)
		printf("cmd_read: complex descriptor retrieved\n");

	/* Get the size of a single acquisition */
	for (i = 0; i < cmd.nb_chan; i++) {
		a4l_chinfo_t *info;

		ret = a4l_get_chinfo(&dsc,
				     cmd.idx_subd, cmd.chan_descs[i], &info);
		if (ret < 0) {
			fprintf(stderr,
				"cmd_read: a4l_get_chinfo failed (ret=%d)\n",
				ret);
			goto out_main;
		}

		if (verbose != 0) {
			printf("cmd_read: channel %x\n", cmd.chan_descs[i]);
			printf("\t ranges count = %d\n", info->nb_rng);
			printf("\t bit width = %d (bits)\n", info->nb_bits);
		}

		scan_size += a4l_sizeof_chan(info);
	}

	if (verbose != 0) {
		printf("cmd_read: scan size = %u\n", scan_size);
		if (cmd.stop_arg != 0)
			printf("cmd_read: size to read = %u\n",
			       scan_size * cmd.stop_arg);
	}

	/* Cancel any former command which might be in progress */
	a4l_snd_cancel(&dsc, cmd.idx_subd);

	if (use_mmap != 0) {

		/* Get the buffer size to map */
		ret = a4l_get_bufsize(&dsc, cmd.idx_subd, &buf_size);
		if (ret < 0) {
			fprintf(stderr,
				"cmd_read: a4l_get_bufsize() failed (ret=%d)\n",
				ret);
			goto out_main;
		}

		if (verbose != 0)
			printf("cmd_read: buffer size = %lu bytes\n", buf_size);

		/* Map the analog input subdevice buffer */
		ret = a4l_mmap(&dsc, cmd.idx_subd, buf_size, &map);
		if (ret < 0) {
			fprintf(stderr,
				"cmd_read: a4l_mmap() failed (ret=%d)\n",
				ret);
			goto out_main;
		}

		if (verbose != 0)
			printf
				("cmd_read: mmap performed successfully (map=0x%p)\n",
				 map);
	}

	ret = a4l_set_wakesize(&dsc, wake_count);
	if (ret < 0) {
		fprintf(stderr,
			"cmd_read: a4l_set_wakesize failed (ret=%d)\n", ret);
		goto out_main;
	}

	if (verbose != 0)
		printf("cmd_read: wake size successfully set (%lu)\n", 
		       wake_count);

	/* Send the command to the input device */
	ret = a4l_snd_command(&dsc, &cmd);
	if (ret < 0) {
		fprintf(stderr,
			"cmd_read: a4l_snd_command failed (ret=%d)\n", ret);
		goto out_main;
	}

	if (verbose != 0)
		printf("cmd_read: command successfully sent\n");

	if (use_mmap == 0) {

		/* Fetch data */
		do {
			/* Perform the read operation */
			ret = a4l_async_read(&dsc, buf, BUF_SIZE, A4L_INFINITE);
			if (ret < 0) {
				fprintf(stderr,
					"cmd_read: a4l_read failed (ret=%d)\n",
					ret);
				goto out_main;
			}

			/* Display the results */
			if (dump_function(&dsc, &cmd, buf, ret) < 0) {
				ret = -EIO;
				goto out_main;
			}

			/* Update the counter */
			cnt += ret;

		} while (ret > 0);

	} else {
		unsigned long front = 0;

		/* Fetch data without any memcpy */
		do {

			/* Retrieve and update the buffer's state
			   (In input case, we recover how many bytes are available
			   to read) */
			ret = a4l_mark_bufrw(&dsc, cmd.idx_subd, front, &front);
			if (ret == -ENOENT)
				break;
			else if (ret < 0) {
				fprintf(stderr,
					"cmd_read: a4l_mark_bufrw() failed (ret=%d)\n",
					ret);
				goto out_main;
			}

			/* If there is nothing to read, wait for an event
			   (Note that a4l_poll() also retrieves the data amount
			   to read; in our case it is useless as we have to update
			   the data read counter) */
			if (front == 0) {
				ret = a4l_poll(&dsc, cmd.idx_subd, A4L_INFINITE);
				if (ret == 0)
					break;
				else if (ret < 0) {
					fprintf(stderr,
						"cmd_read: a4l_poll() failed (ret=%d)\n",
						ret);
					goto out_main;
				}
			}

			/* Display the results */
			if (dump_function(&dsc,
					  &cmd,
					  &((unsigned char *)map)[cnt % buf_size],
					  front) < 0) {
				ret = -EIO;
				goto out_main;
			}

			/* Update the counter */
			cnt += front;

		} while (1);
	}

	if (verbose != 0)
		printf("cmd_read: %d bytes successfully received\n", cnt);

	ret = 0;

out_main:

	if (use_mmap != 0)
		/* Clean the pages table */
		munmap(map, buf_size);

	/* Free the buffer used as device descriptor */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Release the file descriptor */
	a4l_close(&dsc);

	return ret;
}
