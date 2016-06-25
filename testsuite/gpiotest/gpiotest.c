/*
 * Copyright (C) 2016 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <smokey/smokey.h>
#include <rtdm/gpio.h>

smokey_test_plugin(interrupt,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
		   ),
   "Wait for interrupts from a GPIO pin.\n"
   "\tdevice=<device-path>."
);

smokey_test_plugin(read_value,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
		   ),
   "Read GPIO value.\n"
   "\tdevice=<device-path>."
);

smokey_test_plugin(write_value,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
		   ),
   "Write GPIO value.\n"
   "\tdevice=<device-path>."
);

static int run_interrupt(struct smokey_test *t, int argc, char *const argv[])
{
	const char *device = NULL;
	int fd, ret;
	fd_set set;
	
	smokey_parse_args(t, argc, argv);

	if (!SMOKEY_ARG_ISSET(interrupt, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(interrupt, device);
	fd = open(device, O_RDWR);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	ret = ioctl(fd, GPIO_RTIOC_IRQEN);
	if (ret) {
		ret = -errno;
		warning("GPIO_RTIOC_IRQEN failed on %s [%s]",
			device, symerror(ret));
		return ret;
	}

	FD_ZERO(&set);
	FD_SET(fd, &set);
	
	for (;;) {
		ret = select(fd + 1, &set, NULL, NULL, NULL);
		if (ret < 0) {
			ret = -errno;
			warning("failed listening to %s [%s]",
				device, symerror(ret));
		}
		printf("kick %d!\n", ret);
	}

	close(fd);

	return 0;
}

static int run_read_value(struct smokey_test *t, int argc, char *const argv[])
{
	const char *device = NULL;
	int fd, ret, value = -1;

	smokey_parse_args(t, argc, argv);

	if (!SMOKEY_ARG_ISSET(read_value, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(read_value, device);
	fd = open(device, O_RDONLY);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	if (!__T(ret, ioctl(fd, GPIO_RTIOC_DIR_IN)))
		return ret;

	ret = read(fd, &value, sizeof(value));
	close(fd);

	if (!__Tassert(ret == sizeof(value)))
		return -EINVAL;

	smokey_trace("value=%d", value);

	return 0;
}

static int run_write_value(struct smokey_test *t, int argc, char *const argv[])
{
	const char *device = NULL;
	int fd, ret, value;
	
	smokey_parse_args(t, argc, argv);

	if (!SMOKEY_ARG_ISSET(write_value, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(write_value, device);
	fd = open(device, O_WRONLY);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	if (!__T(ret, ioctl(fd, GPIO_RTIOC_DIR_OUT)))
		return ret;
	
	ret = write(fd, &value, sizeof(value));
	close(fd);

	if (!__Tassert(ret == sizeof(value)))
		return -EINVAL;

	return 0;
}

int main(int argc, char *const argv[])
{
	struct smokey_test *t;
	int ret, fails = 0;

	if (pvlist_empty(&smokey_test_list))
		return 0;

	for_each_smokey_test(t) {
		ret = t->run(t, argc, argv);
		if (ret) {
			if (ret == -ENOSYS) {
				smokey_note("%s skipped (no kernel support)",
					    t->name);
				continue;
			}
			fails++;
			if (smokey_keep_going)
				continue;
			if (smokey_verbose_mode)
				error(1, -ret, "test %s failed", t->name);
			return 1;
		}
		smokey_note("%s OK", t->name);
	}

	return fails != 0;
}
