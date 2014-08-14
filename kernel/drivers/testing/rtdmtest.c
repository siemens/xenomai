/*
 * Copyright (C) 2010 Jan Kiszka <jan.kiszka@web.de>.
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

#include <linux/module.h>

#include <rtdm/driver.h>
#include <rtdm/testing.h>

static unsigned int start_index;

module_param(start_index, uint, 0400);
MODULE_PARM_DESC(start_index, "First device instance number to be used");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jan.kiszka@web.de");

struct rtdm_test_context {
	rtdm_timer_t close_timer;
	unsigned long close_counter;
	unsigned long close_deferral;
};

static void close_timer_proc(rtdm_timer_t *timer)
{
	struct rtdm_test_context *ctx =
		container_of(timer, struct rtdm_test_context, close_timer);

	if (ctx->close_counter != 1)
		printk(KERN_ERR
		       "rtdmtest: %s: close_counter is %lu, should be 1!\n",
		       __FUNCTION__, ctx->close_counter);

	rtdm_fd_unlock(rtdm_private_to_fd(ctx));
}

static int rtdm_test_open(struct rtdm_fd *fd, int oflags)
{
	struct rtdm_test_context *ctx = rtdm_fd_to_private(fd);

	rtdm_timer_init(&ctx->close_timer, close_timer_proc,
			"rtdm close test");
	ctx->close_counter = 0;
	ctx->close_deferral = RTTST_RTDM_NORMAL_CLOSE;

	return 0;
}

static void rtdm_test_close(struct rtdm_fd *fd)
{
	struct rtdm_test_context *ctx = rtdm_fd_to_private(fd);

	ctx->close_counter++;

	switch (ctx->close_deferral) {
	case RTTST_RTDM_DEFER_CLOSE_CONTEXT:
		if (ctx->close_counter != 2) {
			printk(KERN_ERR
			       "rtdmtest: %s: close_counter is %lu, "
			       "should be 2!\n",
			       __FUNCTION__, ctx->close_counter);
			return;
		}
		break;
	}

	rtdm_timer_destroy(&ctx->close_timer);
}

static int
rtdm_test_ioctl(struct rtdm_fd *fd, unsigned int request, void __user *arg)
{
	struct rtdm_test_context *ctx = rtdm_fd_to_private(fd);
	int err = 0;

	switch (request) {
	case RTTST_RTIOC_RTDM_DEFER_CLOSE:
		ctx->close_deferral = (unsigned long)arg;
		if (ctx->close_deferral == RTTST_RTDM_DEFER_CLOSE_CONTEXT) {
			++ctx->close_counter;
			rtdm_fd_lock(fd);
			rtdm_timer_start(&ctx->close_timer, 300000000ULL, 0,
					RTDM_TIMERMODE_RELATIVE);
		}
		break;

	default:
		err = -ENOTTY;
	}

	return err;
}

static struct rtdm_device device[2] = { [0 ... 1] = {
	.struct_version		= RTDM_DEVICE_STRUCT_VER,
	.device_flags		= RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,
	.context_size		= sizeof(struct rtdm_test_context),
	.device_name		= "",
	.ops = {
		.open		= rtdm_test_open,
		.close		= rtdm_test_close,
		.ioctl_rt	= rtdm_test_ioctl,
		.ioctl_nrt	= rtdm_test_ioctl,
	},
	.device_class		= RTDM_CLASS_TESTING,
	.device_sub_class	= RTDM_SUBCLASS_RTDMTEST,
	.profile_version	= RTTST_PROFILE_VER,
	.driver_name		= "xeno_rtdmtest",
	.driver_version		= RTDM_DRIVER_VER(0, 1, 0),
	.peripheral_name	= "RTDM unit test",
	.provider_name		= "Jan Kiszka",
} };

static int __init __rtdm_test_init(void)
{
	int dev = 0;
	int err;

	if (!realtime_core_enabled())
		return -ENODEV;

	while (1) {
		device[dev].proc_name = device[dev].device_name;

		ksformat(device[dev].device_name, RTDM_MAX_DEVNAME_LEN,
			 "rttest-rtdm%d",
			 start_index);
		err = rtdm_dev_register(&device[dev]);

		start_index++;

		if (!err) {
			if (++dev >= ARRAY_SIZE(device))
				break;
		} else if (err != -EEXIST) {
			while (dev > 0) {
				dev--;
				rtdm_dev_unregister(&device[dev], 1000);
			}
			return err;
		}
	}
	return 0;
}

static void __exit __rtdm_test_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(device); i++)
		rtdm_dev_unregister(&device[i], 1000);
}

module_init(__rtdm_test_init);
module_exit(__rtdm_test_exit);
