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
#include <signal.h>
#include <pthread.h>
#include <rtdm/analogy.h>

typedef int (*dump_function_t) (a4l_desc_t *, a4l_cmd_t*, unsigned char *, int);
static pthread_t thread;
struct arguments {
	int argc;
	char **argv;
};

#define MAX_NB_CHAN 32
#define NB_SCAN 100
#define ID_SUBD 0

#define FILENAME "analogy0"
#define BUF_SIZE 10000

static unsigned int chans[MAX_NB_CHAN];
static unsigned char buf[BUF_SIZE];
static char *str_chans = "0,1,2,3";
static char *filename = FILENAME;

static unsigned long wake_count = 0;
static int real_time = 0;
static int use_mmap = 0;
static int verbose = 0;

#define ERR(fmt, args ...) fprintf(stderr, fmt, ##args)
#define OUT(fmt, args ...) fprintf(stdout, fmt, ##args)
#define DBG(fmt, args...) 						\
        do {								\
	        if (verbose) 						\
		           printf(fmt, ##args);				\
	} while (0)


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

static void do_print_usage(void)
{
	OUT("usage:\tcmd_read [OPTS]\n");
	OUT("\tOPTS:\t -v, --verbose: verbose output\n");
	OUT("\t\t -r, --real-time: enable real-time acquisition mode\n");
	OUT("\t\t -d, --device: device filename (analogy0, analogy1, ...)\n");
	OUT("\t\t -s, --subdevice: subdevice index\n");
	OUT("\t\t -S, --scan-count: count of scan to perform\n");
	OUT("\t\t -c, --channels: channels to use (ex.: -c 0,1)\n");
	OUT("\t\t -m, --mmap: mmap the buffer\n");
	OUT("\t\t -w, --raw: dump data in raw format\n");
	OUT("\t\t -k, --wake-count: space available before waking up the process\n");
	OUT("\t\t -h, --help: print this help\n");
}

static int dump_raw(a4l_desc_t *dsc, a4l_cmd_t *cmd, unsigned char *buf, int size)
{
	return fwrite(buf, size, 1, stdout);
}

static int dump_text(a4l_desc_t *dsc, a4l_cmd_t *cmd, unsigned char *buf, int size)
{
	a4l_chinfo_t *chans[MAX_NB_CHAN];
	int i, err = 0, tmp_size = 0;
	char *fmts[MAX_NB_CHAN];
	static int cur_chan;

	for (i = 0; i < cmd->nb_chan; i++) {
		int width;

		err = a4l_get_chinfo(dsc, cmd->idx_subd, cmd->chan_descs[i], &chans[i]);
		if (err < 0) {
			ERR("cmd_read: a4l_get_chinfo failed (ret=%d)\n", err);
			goto out;
		}

		width = a4l_sizeof_chan(chans[i]);
		if (width < 0) {
			ERR("cmd_read: incoherent info for channel %d\n", cmd->chan_descs[i]);
			err = width;
			goto out;
		}

		switch (width) {
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

		OUT(fmts[cur_chan], value);

		/* We assume a4l_sizeof_chan() cannot return because we already
		 * called it on the very same channel descriptor */
		tmp_size += a4l_sizeof_chan(chans[cur_chan]);

		if (++cur_chan == cmd->nb_chan) {
			OUT("\n");
			cur_chan = 0;
		}
	}

	fflush(stdout);
out:
	return err;
}

static int fetch_data(a4l_desc_t *dsc, void *buf, unsigned int *cnt, dump_function_t dump)
{
	int ret;

	for (;;) {
		ret = a4l_async_read(dsc, buf, BUF_SIZE, A4L_INFINITE);

		if (ret == 0) {
			DBG("cmd_read: no more data in the buffer \n");
			break;
		}

		if (ret < 0) {
			ERR("cmd_read: a4l_read failed (ret=%d)\n", ret);
			return ret;
		}

		*cnt += ret;

		ret = dump(dsc, &cmd, buf, ret);
		if (ret < 0)
			return -EIO;
	}

	return ret;
}

static int fetch_data_mmap(a4l_desc_t *dsc, unsigned int *cnt, dump_function_t dump,
			   void *map, unsigned long buf_size)
{
	unsigned long cnt_current = 0, cnt_updated = 0;
	int ret;

	for (;;) {

		/* Retrieve and update the buffer's state
		 * In input case, recover how many bytes are available to read
		 */
		ret = a4l_mark_bufrw(dsc, cmd.idx_subd, cnt_current, &cnt_updated);

		if (ret == -ENOENT)
			break;

		if (ret < 0) {
			ERR("cmd_read: a4l_mark_bufrw() failed (ret=%d)\n", ret);
			return ret;
		}

		/* If there is nothing to read, wait for an event
		   (Note that a4l_poll() also retrieves the data amount
		   to read; in our case it is useless as we have to update
		   the data read counter) */
		if (!cnt_updated) {
			ret = a4l_poll(dsc, cmd.idx_subd, A4L_INFINITE);

			if (ret == 0)
				break;

			if (ret < 0) {
				ERR("cmd_read: a4l_poll() failed (ret=%d)\n", ret);
				return ret;
			}

			cnt_current = cnt_updated;
			continue;
		}

		ret = dump(dsc, &cmd, map + (*cnt % buf_size), cnt_updated);
		if (ret < 0)
			return -EIO;

		*cnt += cnt_updated;
		cnt_current = cnt_updated;
	}

	return 0;
}

static int map_subdevice_buffer(a4l_desc_t *dsc, unsigned long *buf_size, void **map)
{
	void *buf;
	int ret;

	/* Get the buffer size to map */
	ret = a4l_get_bufsize(dsc, cmd.idx_subd, buf_size);
	if (ret < 0) {
		ERR("cmd_read: a4l_get_bufsize() failed (ret=%d)\n", ret);
		return ret;
	}
	DBG("cmd_read: buffer size = %lu bytes\n", *buf_size);

	/* Map the analog input subdevice buffer */
	ret = a4l_mmap(dsc, cmd.idx_subd, *buf_size, &buf);
	if (ret < 0) {
		ERR("cmd_read: a4l_mmap() failed (ret=%d)\n", ret);
		return ret;
	}
	DBG("cmd_read: mmap performed successfully (map=0x%p)\n", buf);
	*map = buf;

	return 0;
}

static void *cmd_read(void *arg)
{
	unsigned int i, scan_size = 0, cnt = 0, ret = 0, len, ofs;
	dump_function_t dump_function = dump_text;
	a4l_desc_t dsc = { .sbdata = NULL };
	unsigned long buf_size;
	void *map = NULL;

	struct arguments *p = arg;
	char **argv = p->argv;
	int argc = p->argc;

	for (;;) {
		ret = getopt_long(argc, argv, "vrd:s:S:c:mwk:h",
				  cmd_read_opts, NULL);

		if (ret == -1)
			break;

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
			pthread_exit(0);
		}
	}

	if (isatty(STDOUT_FILENO) && dump_function == dump_raw) {
		ERR("cmd_read: cannot dump raw data on a terminal\n\n");
		ret = -EINVAL;
		pthread_exit(&ret);
	}

	/* Recover the channels to compute */
	do {
		cmd.nb_chan++;
		len = strlen(str_chans);
		ofs = strcspn(str_chans, ",");
		if (sscanf(str_chans, "%u", &chans[cmd.nb_chan - 1]) == 0) {
			ERR("cmd_read: bad channel argument\n");
			ret = -EINVAL;
			pthread_exit(&ret);
		}
		str_chans += ofs + 1;
	} while (len != ofs);

	/* Update the command structure */
	cmd.scan_end_arg = cmd.nb_chan;
	cmd.stop_src = cmd.stop_arg != 0 ? TRIG_COUNT : TRIG_NONE;

	ret = a4l_open(&dsc, filename);
	if (ret < 0) {
		ERR("cmd_read: a4l_open %s failed (ret=%d)\n", filename, ret);
		pthread_exit(&ret);
	}

	DBG("cmd_read: device %s opened (fd=%d)\n", filename, dsc.fd);
	DBG("cmd_read: basic descriptor retrieved\n");
	DBG("\t subdevices count = %d\n", dsc.nb_subd);
	DBG("\t read subdevice index = %d\n", dsc.idx_read_subd);
	DBG("\t write subdevice index = %d\n", dsc.idx_write_subd);

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL) {
		ERR("cmd_read: malloc failed \n");
		ret = -ENOMEM;
		pthread_exit(&ret);
	}

	/* Get this data */
	ret = a4l_fill_desc(&dsc);
	if (ret < 0) {
		ERR("cmd_read: a4l_fill_desc failed (ret=%d)\n", ret);
		goto out;
	}
	DBG("cmd_read: complex descriptor retrieved\n");

	/* Get the size of a single acquisition */
	for (i = 0; i < cmd.nb_chan; i++) {
		a4l_chinfo_t *info;
		ret = a4l_get_chinfo(&dsc,cmd.idx_subd, cmd.chan_descs[i], &info);
		if (ret < 0) {
			ERR("cmd_read: a4l_get_chinfo failed (ret=%d)\n", ret);
			goto out;
		}

		DBG("cmd_read: channel %x\n", cmd.chan_descs[i]);
		DBG("\t ranges count = %d\n", info->nb_rng);
		DBG("\t bit width = %d (bits)\n", info->nb_bits);

		scan_size += a4l_sizeof_chan(info);
	}
	DBG("cmd_read: scan size = %u\n", scan_size);
	DBG("cmd_read: size to read = %u\n", scan_size * cmd.stop_arg);

	/* Cancel any former command which might be in progress */
	a4l_snd_cancel(&dsc, cmd.idx_subd);

	if (use_mmap) {
		ret = map_subdevice_buffer(&dsc, &buf_size, &map);
		if (ret)
			goto out;
	}

	ret = a4l_set_wakesize(&dsc, wake_count);
	if (ret < 0) {
		ERR("cmd_read: a4l_set_wakesize failed (ret=%d)\n", ret);
		goto out;
	}
	DBG("cmd_read: wake size successfully set (%lu)\n", wake_count);

	/* Send the command to the input device */
	ret = a4l_snd_command(&dsc, &cmd);
	if (ret < 0) {
		ERR("cmd_read: a4l_snd_command failed (ret=%d)\n", ret);
		goto out;
	}
	DBG("cmd_read: command successfully sent\n");

	if (!use_mmap) {
		ret = fetch_data(&dsc, buf, &cnt, dump_function);
		if (ret) {
			ERR("cmd_read: failed to fetch_data (ret=%d)\n", ret);
			goto out;
		}
	}
	else {
		ret = fetch_data_mmap(&dsc, &cnt, dump_function, map, buf_size);
		if (ret) {
			ERR("cmd_read: failed to fetch_data_mmap (ret=%d)\n", ret);
			goto out;
		}
	}
	DBG("cmd_read: %d bytes successfully received\n", cnt);

	pthread_exit(NULL);

	return NULL;
out:
	if (use_mmap != 0)
		munmap(map, buf_size);

	/* Free the buffer used as device descriptor */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Release the file descriptor */
	a4l_close(&dsc);

	pthread_exit(&ret);
}

static void cleanup_upon_sig(int sig)
{
	pthread_cancel(thread);
	signal(sig, SIG_DFL);
}

int main(int argc, char *argv[])
{
	struct sched_param param = {.sched_priority = 99};
	struct arguments args = {.argc = argc, .argv = argv};
	pthread_attr_t attr;
	sigset_t mask;
	int ret;

	sigemptyset(&mask);

	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);

	signal(SIGTERM, cleanup_upon_sig);
	signal(SIGINT, cleanup_upon_sig);
	signal(SIGHUP, cleanup_upon_sig);

	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	ret = mlockall(MCL_CURRENT | MCL_FUTURE);
	if (ret < 0) {
		ret = errno;
		ERR("cmd_read: mlockall failed (ret=%d)\n", ret);
		return -1;
	}

	/* delegate all the processing to a realtime thread */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

	ret = pthread_attr_setschedparam(&attr, &param);
	if (ret) {
		ERR("pthread_attr_setschedparam failed (ret=%d)\n", ret);
		return -1;
	}

	ret = pthread_create(&thread, &attr, &cmd_read, &args);
	if (ret) {
		ERR("pthread_create failed (ret=%d)\n", ret);
		return -1;
	}

	pthread_join(thread, (void **) &ret);
	if (ret)
		ERR("cmd_read failed (ret=%d) \n", ret);

	return ret;
}
