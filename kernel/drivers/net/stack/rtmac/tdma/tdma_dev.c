/***
 *
 *  rtmac/tdma/tdma_dev.c
 *
 *  RTmac - real-time networking media access control subsystem
 *  Copyright (C) 2002      Marc Kleine-Budde <kleine-budde@gmx.de>
 *                2003-2006 Jan Kiszka <Jan.Kiszka@web.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/list.h>

#include <rtdev.h>
#include <rtmac.h>
#include <rtmac/tdma/tdma.h>


struct tdma_dev_ctx {
    rtdm_task_t *cycle_waiter;
};


static int tdma_dev_open(struct rtdm_dev_context *context,
                         rtdm_user_info_t *user_info, int oflags)
{
    struct tdma_dev_ctx *ctx = (struct tdma_dev_ctx *)context->dev_private;


    ctx->cycle_waiter = NULL;

    return 0;
}


static int tdma_dev_close(struct rtdm_dev_context *context,
                          rtdm_user_info_t *user_info)
{
    struct tdma_dev_ctx *ctx = (struct tdma_dev_ctx *)context->dev_private;


    RTDM_EXECUTE_ATOMICALLY(
        if (ctx->cycle_waiter)
            rtdm_task_unblock(ctx->cycle_waiter);
    );

    return 0;
}


static int wait_on_sync(struct tdma_dev_ctx *tdma_ctx,
                        rtdm_event_t *sync_event)
{
    int ret;


    RTDM_EXECUTE_ATOMICALLY(
        /* keep it simple: only one waiter per device instance allowed */
        if (!tdma_ctx->cycle_waiter) {
            tdma_ctx->cycle_waiter = rtdm_task_current();
            ret = rtdm_event_wait(sync_event);
            tdma_ctx->cycle_waiter = NULL;
        } else
            ret = -EBUSY;
    );
    return ret;
}


static int tdma_dev_ioctl(struct rtdm_dev_context *context,
                          rtdm_user_info_t *user_info,
                          unsigned int request, void *arg)
{
    struct tdma_dev_ctx *ctx = (struct tdma_dev_ctx *)context->dev_private;
    struct tdma_priv    *tdma;
    rtdm_lockctx_t      lock_ctx;
    int                 ret;


    tdma = container_of((struct rtdm_device *)context->device,
                        struct tdma_priv, api_device);

    switch (request) {
        case RTMAC_RTIOC_TIMEOFFSET: {
            nanosecs_rel_t offset;

            rtdm_lock_get_irqsave(&tdma->lock, lock_ctx);
            offset = tdma->clock_offset;
            rtdm_lock_put_irqrestore(&tdma->lock, lock_ctx);

            if (user_info) {
                if (!rtdm_rw_user_ok(user_info, arg, sizeof(__s64)) ||
                    rtdm_copy_to_user(user_info, arg, &offset, sizeof(__s64)))
                    return -EFAULT;
            } else
                *(__s64 *)arg = offset;

            return 0;
        }
        case RTMAC_RTIOC_WAITONCYCLE:
            if (!rtdm_in_rt_context())
                return -ENOSYS;

            if ((long)arg !=TDMA_WAIT_ON_SYNC)
                return -EINVAL;

            return wait_on_sync(ctx, &tdma->sync_event);

        case RTMAC_RTIOC_WAITONCYCLE_EX: {
            struct rtmac_waitinfo   *waitinfo = (struct rtmac_waitinfo *)arg;
            struct rtmac_waitinfo   waitinfo_buf;

            #define WAITINFO_HEAD_SIZE \
                ((char *)&waitinfo_buf.cycle_no - (char *)&waitinfo_buf)

            if (!rtdm_in_rt_context())
                return -ENOSYS;

            if (user_info) {
                if (!rtdm_rw_user_ok(user_info, waitinfo,
                                     sizeof(struct rtmac_waitinfo)) ||
                    rtdm_copy_from_user(user_info, &waitinfo_buf, arg,
                                        WAITINFO_HEAD_SIZE))
                    return -EFAULT;

                waitinfo = &waitinfo_buf;
            }

            if ((waitinfo->type != TDMA_WAIT_ON_SYNC) ||
                (waitinfo->size < sizeof(struct rtmac_waitinfo)))
                return -EINVAL;

            ret = wait_on_sync(ctx, &tdma->sync_event);
            if (ret)
                return ret;

            rtdm_lock_get_irqsave(&tdma->lock, lock_ctx);
            waitinfo->cycle_no     = tdma->current_cycle;
            waitinfo->cycle_start  = tdma->current_cycle_start;
            waitinfo->clock_offset = tdma->clock_offset;
            rtdm_lock_put_irqrestore(&tdma->lock, lock_ctx);

            if (user_info) {
                if (rtdm_copy_to_user(user_info, arg, &waitinfo_buf,
                                      sizeof(struct rtmac_waitinfo)))
                    return -EFAULT;
            }

            return 0;
        }
        default:
            return -ENOTTY;
    }
}


int tdma_dev_init(struct rtnet_device *rtdev, struct tdma_priv *tdma)
{
    char    *pos;


    tdma->api_device.struct_version = RTDM_DEVICE_STRUCT_VER;

    tdma->api_device.device_flags = RTDM_NAMED_DEVICE;
    tdma->api_device.context_size = sizeof(struct tdma_dev_ctx);

    strcpy(tdma->api_device.device_name, "TDMA");
    for (pos = rtdev->name + strlen(rtdev->name) - 1;
        (pos >= rtdev->name) && ((*pos) >= '0') && (*pos <= '9'); pos--);
    strncat(tdma->api_device.device_name+4, pos+1, IFNAMSIZ-4);

    tdma->api_device.open_nrt = tdma_dev_open;

    tdma->api_device.ops.close_nrt = tdma_dev_close;

    tdma->api_device.ops.ioctl_rt  = tdma_dev_ioctl;
    tdma->api_device.ops.ioctl_nrt = tdma_dev_ioctl;

    tdma->api_device.proc_name = tdma->api_device.device_name;

    tdma->api_device.device_class     = RTDM_CLASS_RTMAC;
    tdma->api_device.device_sub_class = RTDM_SUBCLASS_TDMA;
    tdma->api_device.driver_name      = "tdma";
    tdma->api_device.driver_version   = RTNET_RTDM_VER;
    tdma->api_device.peripheral_name  = "TDMA API";
    tdma->api_device.provider_name    = rtnet_rtdm_provider_name;

    return rtdm_dev_register(&tdma->api_device);
}
