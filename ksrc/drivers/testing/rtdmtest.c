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

#include <rtdm/rtdm_driver.h>
#include <rtdm/rttesting.h>

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

	rtdm_context_unlock(rtdm_private_to_context(ctx));
}

static int rtdm_test_open(struct rtdm_dev_context *context,
			  rtdm_user_info_t *user_info, int oflags)
{
	struct rtdm_test_context *ctx =
		(struct rtdm_test_context *)context->dev_private;

	rtdm_timer_init(&ctx->close_timer, close_timer_proc,
			"rtdm close test");
	ctx->close_counter = 0;
	ctx->close_deferral = RTTST_RTDM_NORMAL_CLOSE;

	return 0;
}

static int rtdm_test_close(struct rtdm_dev_context *context,
			   rtdm_user_info_t *user_info)
{
	struct rtdm_test_context *ctx =
		(struct rtdm_test_context *)context->dev_private;

	ctx->close_counter++;

	switch (ctx->close_deferral) {
	case RTTST_RTDM_DEFER_CLOSE_HANDLER:
		if (ctx->close_counter <= 3)
			return -EAGAIN;
		if (ctx->close_counter > 4) {
			printk(KERN_ERR
			       "rtdmtest: %s: close_counter is %lu, "
			       "should be 2!\n",
			       __FUNCTION__, ctx->close_counter);
			return 0;
		}
		break;

	case RTTST_RTDM_DEFER_CLOSE_CONTEXT:
		if (ctx->close_counter == 1) {
			rtdm_context_lock(context);
			rtdm_timer_start(&ctx->close_timer, 300000000ULL, 0,
					 RTDM_TIMERMODE_RELATIVE);
			return 0;
		}
		if (ctx->close_counter > 2) {
			printk(KERN_ERR
			       "rtdmtest: %s: close_counter is %lu, "
			       "should be 2!\n",
			       __FUNCTION__, ctx->close_counter);
			return 0;
		}
		break;
	}

	rtdm_timer_destroy(&ctx->close_timer);

	return 0;
}

static int rtdm_test_ioctl(struct rtdm_dev_context *context,
			   rtdm_user_info_t *user_info,
			   unsigned int request, void __user *arg)
{
	struct rtdm_test_context *ctx =
		(struct rtdm_test_context *)context->dev_private;
	int err = 0;

	switch (request) {
	case RTTST_RTIOC_RTDM_DEFER_CLOSE:
		ctx->close_deferral = (unsigned long)arg;
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

	.open_nrt		= rtdm_test_open,

	.ops = {
		.close_nrt	= rtdm_test_close,

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

	while (1) {
		device[dev].proc_name = device[dev].device_name;

		snprintf(device[dev].device_name, RTDM_MAX_DEVNAME_LEN,
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
