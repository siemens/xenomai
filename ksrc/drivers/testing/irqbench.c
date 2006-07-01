/*
 * Copyright (C) 2006 Jan Kiszka <jan.kiszka@web.de>.
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
#include <asm/semaphore.h>

#include <rtdm/rttesting.h>
#include <rtdm/rtdm_driver.h>
#include <nucleus/trace.h>

/* --- Serial port --- */

#define MSR_DCTS                0x01
#define MSR_DDSR                0x02
#define MSR_DDCD                0x08

#define MCR_RTS                 0x02
#define MCR_OUT2                0x08

#define IER_MODEM               0x08

#define RHR(ctx) (ctx->port_ioaddr + 0) /* Receive Holding Buffer */
#define IER(ctx) (ctx->port_ioaddr + 1) /* Interrupt Enable Register */
#define IIR(ctx) (ctx->port_ioaddr + 2) /* Interrupt Id Register */
#define LCR(ctx) (ctx->port_ioaddr + 3) /* Line Control Register */
#define MCR(ctx) (ctx->port_ioaddr + 4) /* Modem Control Register */
#define LSR(ctx) (ctx->port_ioaddr + 5) /* Line Status Register */
#define MSR(ctx) (ctx->port_ioaddr + 6) /* Modem Status Register */

/* --- Parallel port --- */

#define CTRL_INIT               0x04

#define STAT_STROBE             0x10

#define DATA(ctx) (ctx->port_ioaddr + 0) /* Data register */
#define STAT(ctx) (ctx->port_ioaddr + 1) /* Status register */
#define CTRL(ctx) (ctx->port_ioaddr + 2) /* Control register */

struct rt_irqbench_context {
    int                         mode;
    int                         port_type;
    unsigned long               port_ioaddr;
    unsigned int                toggle;
    struct rttst_irqbench_stats stats;
    rtdm_irq_t                  irq_handle;
    rtdm_event_t                irq_event;
    rtdm_task_t                 irq_task;
    struct semaphore            nrt_mutex;
};

static unsigned int start_index;

module_param(start_index, uint, 0400);
MODULE_PARM_DESC(start_index, "First device instance number to be used");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jan.kiszka@web.de");


static inline int rt_irqbench_check_irq(struct rt_irqbench_context *ctx)
{
    int status;


    switch (ctx->port_type) {
        case RTTST_IRQBENCH_SERPORT:
            status = inb(MSR(ctx));
            if (status & (MSR_DDSR | MSR_DDCD))
                xntrace_user_freeze(0, 0);
            if (!(status & MSR_DCTS))
                return 0;
            break;

        case RTTST_IRQBENCH_PARPORT:
            // todo
            break;
    }
    ctx->stats.irqs_received++;
    return 1;
}


static inline void rt_irqbench_hwreply(struct rt_irqbench_context *ctx)
{
    switch (ctx->port_type) {
        case RTTST_IRQBENCH_SERPORT:
            /* toggle RTS */
            ctx->toggle ^= MCR_RTS;
            outb(ctx->toggle, MCR(ctx));
            break;

        case RTTST_IRQBENCH_PARPORT:
            ctx->toggle ^= 0xFF;
            outb(ctx->toggle, DATA(ctx));
            break;
    }
    xntrace_special(0xBE, 0);
    ctx->stats.irqs_acknowledged++;
}


static void rt_irqbench_task(void *arg)
{
    struct rt_irqbench_context *ctx = (struct rt_irqbench_context *)arg;


    while (1) {
        if (rtdm_event_wait(&ctx->irq_event) < 0)
            return;
        rt_irqbench_hwreply(ctx);
    }
}


static int rt_irqbench_task_irq(rtdm_irq_t *irq_handle)
{
    struct rt_irqbench_context *ctx;


    ctx = rtdm_irq_get_arg(irq_handle, struct rt_irqbench_context);

    if (rt_irqbench_check_irq(ctx))
        rtdm_event_signal(&ctx->irq_event);

    return RTDM_IRQ_HANDLED;
}


static int rt_irqbench_direct_irq(rtdm_irq_t *irq_handle)
{
    struct rt_irqbench_context *ctx;


    ctx = rtdm_irq_get_arg(irq_handle, struct rt_irqbench_context);

    if (rt_irqbench_check_irq(ctx))
        rt_irqbench_hwreply(ctx);

    return RTDM_IRQ_HANDLED;
}


static int rt_irqbench_stop(struct rt_irqbench_context *ctx)
{
    if (ctx->mode < 0)
        return -EINVAL;

    /* Disable hardware */
    switch (ctx->port_type) {
        case RTTST_IRQBENCH_SERPORT:
            outb(0, IER(ctx));
            break;

        case RTTST_IRQBENCH_PARPORT:
            outb(0, CTRL(ctx));
            break;
    }

    rtdm_irq_free(&ctx->irq_handle);

    if (ctx->mode == RTTST_IRQBENCH_KERNEL_TASK)
        rtdm_task_destroy(&ctx->irq_task);

    ctx->mode = -1;

    return 0;
}


static int rt_irqbench_open(struct rtdm_dev_context *context,
                            rtdm_user_info_t *user_info, int oflags)
{
    struct rt_irqbench_context  *ctx;


    ctx = (struct rt_irqbench_context *)context->dev_private;

    ctx->mode = -1;
    rtdm_event_init(&ctx->irq_event, 0);
    init_MUTEX(&ctx->nrt_mutex);

    return 0;
}


static int rt_irqbench_close(struct rtdm_dev_context *context,
                             rtdm_user_info_t *user_info)
{
    struct rt_irqbench_context  *ctx;


    ctx = (struct rt_irqbench_context *)context->dev_private;

    down(&ctx->nrt_mutex);
    rt_irqbench_stop(ctx);
    rtdm_event_destroy(&ctx->irq_event);
    up(&ctx->nrt_mutex);

    return 0;
}


static int rt_irqbench_ioctl_nrt(struct rtdm_dev_context *context,
                                 rtdm_user_info_t *user_info, int request,
                                 void *arg)
{
    struct rt_irqbench_context  *ctx;
    int                         ret = 0;


    ctx = (struct rt_irqbench_context *)context->dev_private;

    switch (request) {
        case RTTST_RTIOC_IRQBENCH_START: {
            struct rttst_irqbench_config config_buf;
            struct rttst_irqbench_config *config;

            config = (struct rttst_irqbench_config *)arg;
            if (user_info) {
                if (!rtdm_read_user_ok(user_info, arg,
                                    sizeof(struct rttst_irqbench_config)) ||
                    rtdm_copy_from_user(user_info, &config_buf, arg,
                                        sizeof(struct rttst_irqbench_config)))
                    return -EFAULT;

                config = &config_buf;
            }

            if (config->port_type > RTTST_IRQBENCH_PARPORT)
                return -EINVAL;

            down(&ctx->nrt_mutex);

            if (test_bit(RTDM_CLOSING, &context->context_flags))
                goto unlock_start_out;

            ctx->port_type   = config->port_type;
            ctx->port_ioaddr = config->port_ioaddr;

            /* Initialise hardware */
            switch (ctx->port_type) {
                case RTTST_IRQBENCH_SERPORT:
                    ctx->toggle = MCR_OUT2;

                    /* Reset DLAB, reset RTS, enable OUT2 */
                    outb(0, LCR(ctx));
                    outb(MCR_OUT2, MCR(ctx));

                    /* Mask all UART interrupts and clear pending ones. */
                    outb(0, IER(ctx));
                    inb(IIR(ctx));
                    inb(LSR(ctx));
                    inb(RHR(ctx));
                    inb(MSR(ctx));
                    break;

                case RTTST_IRQBENCH_PARPORT:
                    ctx->toggle = 0xAA;
                    outb(0xAA, DATA(ctx));
                    outb(CTRL_INIT, CTRL(ctx));
                    break;
            }

            switch (config->mode) {
                case RTTST_IRQBENCH_USER_TASK:
                    ret = rtdm_irq_request(&ctx->irq_handle, config->port_irq,
                                           rt_irqbench_task_irq, 0,
                                           "irqbench", ctx);
                    break;

                case RTTST_IRQBENCH_KERNEL_TASK:
                    ret = rtdm_irq_request(&ctx->irq_handle, config->port_irq,
                                           rt_irqbench_task_irq, 0,
                                           "irqbench", ctx);
                    if (ret < 0)
                        goto unlock_start_out;

                    ret = rtdm_task_init(&ctx->irq_task, "irqbench",
                                         rt_irqbench_task, ctx,
                                         config->priority, 0);
                    if (ret < 0)
                        rtdm_irq_free(&ctx->irq_handle);
                    break;

                case RTTST_IRQBENCH_HANDLER:
                    ret = rtdm_irq_request(&ctx->irq_handle, config->port_irq,
                                           rt_irqbench_direct_irq, 0,
                                           "irqbench", ctx);
                    break;

                default:
                    ret = -EINVAL;
                    break;
            }
            if (ret < 0)
                goto unlock_start_out;

            ctx->mode = config->mode;

            memset(&ctx->stats, 0, sizeof(ctx->stats));

            rtdm_irq_enable(&ctx->irq_handle);

            /* Arm IRQ */
            switch (ctx->port_type) {
                case RTTST_IRQBENCH_SERPORT:
                    outb(IER_MODEM, IER(ctx));
                    break;

                case RTTST_IRQBENCH_PARPORT:
                    outb(STAT_STROBE, CTRL(ctx));
                    break;
            }

          unlock_start_out:
            up(&ctx->nrt_mutex);
            break;
        }

        case RTTST_RTIOC_IRQBENCH_STOP:
            down(&ctx->nrt_mutex);
            ret = rt_irqbench_stop(ctx);
            up(&ctx->nrt_mutex);
            break;

        case RTTST_RTIOC_IRQBENCH_GET_STATS: {
            struct rttst_irqbench_stats *usr_stats;

            usr_stats = (struct rttst_irqbench_stats *)arg;

            if (user_info) {
                if (!rtdm_rw_user_ok(user_info, usr_stats,
                                     sizeof(struct rttst_irqbench_stats)) ||
                    rtdm_copy_to_user(user_info, usr_stats,
                                      &ctx->stats,
                                      sizeof(struct rttst_irqbench_stats)))
                    ret = -EFAULT;
            } else
                *usr_stats = ctx->stats;
            break;
        }

        case RTTST_RTIOC_IRQBENCH_WAIT_IRQ:
            ret = -ENOSYS;
            break;

        case RTTST_RTIOC_IRQBENCH_REPLY_IRQ:
            rt_irqbench_hwreply(ctx);
            break;

        default:
            ret = -ENOTTY;
    }

    return ret;
}


static int rt_irqbench_ioctl_rt(struct rtdm_dev_context *context,
                                rtdm_user_info_t *user_info, int request,
                                void *arg)
{
    struct rt_irqbench_context  *ctx;
    int                         ret = 0;


    ctx = (struct rt_irqbench_context *)context->dev_private;

    switch (request) {
        case RTTST_RTIOC_IRQBENCH_WAIT_IRQ:
            ret = rtdm_event_wait(&ctx->irq_event);
            break;

        case RTTST_RTIOC_IRQBENCH_REPLY_IRQ:
            rt_irqbench_hwreply(ctx);
            break;

        case RTTST_RTIOC_IRQBENCH_START:
        case RTTST_RTIOC_IRQBENCH_STOP:
        case RTTST_RTIOC_IRQBENCH_GET_STATS:
            ret = -ENOSYS;
            break;

        default:
            ret = -ENOTTY;
    }

    return ret;
}


static struct rtdm_device device = {
    struct_version:     RTDM_DEVICE_STRUCT_VER,

    device_flags:       RTDM_NAMED_DEVICE,
    context_size:       sizeof(struct rt_irqbench_context),
    device_name:        "",

    open_rt:            NULL,
    open_nrt:           rt_irqbench_open,

    ops: {
        close_rt:       NULL,
        close_nrt:      rt_irqbench_close,

        ioctl_rt:       rt_irqbench_ioctl_rt,
        ioctl_nrt:      rt_irqbench_ioctl_nrt,

        read_rt:        NULL,
        read_nrt:       NULL,

        write_rt:       NULL,
        write_nrt:      NULL,

        recvmsg_rt:     NULL,
        recvmsg_nrt:    NULL,

        sendmsg_rt:     NULL,
        sendmsg_nrt:    NULL,
    },

    device_class:       RTDM_CLASS_TESTING,
    device_sub_class:   RTDM_SUBCLASS_IRQBENCH,
    driver_name:        "xeno_irqbench",
    driver_version:     RTDM_DRIVER_VER(0, 1, 0),
    peripheral_name:    "IRQ Latency Benchmark",
    provider_name:      "Jan Kiszka",
    proc_name:          device.device_name,
};

int __init __irqbench_init(void)
{
    int ret;

    do {
        snprintf(device.device_name, RTDM_MAX_DEVNAME_LEN, "rttest%d",
                 start_index);
        ret = rtdm_dev_register(&device);

        start_index++;
    } while (ret == -EEXIST);

    return ret;
}


void __exit __irqbench_exit(void)
{
    rtdm_dev_unregister(&device, 1000);
}


module_init(__irqbench_init);
module_exit(__irqbench_exit);
