/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
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
#include <linux/ioport.h>
#include <asm/io.h>

#include <xenomai/rtdm/rtserial.h>
#include <xenomai/rtdm/rtdm_driver.h>


#define MAX_DEVICES         8

#define IN_BUFFER_SIZE      4096
#define OUT_BUFFER_SIZE     4096

#define DEFAULT_BAUD_BASE   115200
#define DEFAULT_TX_FIFO     16

#define PARITY_MASK         0x03
#define DATA_BITS_MASK      0x03
#define STOP_BITS_MASK      0x01
#define FIFO_MASK           0xC0
#define EVENT_MASK          0x0F

#define LCR_DLAB            0x80

#define FCR_FIFO            0x01
#define FCR_RESET           0x06

#define IER_RX              0x01
#define IER_TX              0x02
#define IER_STAT            0x04
#define IER_MODEM           0x08

#define IIR_MODEM           0x00
#define IIR_PIRQ            0x01
#define IIR_TX              0x02
#define IIR_RX              0x04
#define IIR_STAT            0x06
#define IIR_TMO             0x0C
#define IIR_MASK            0x0F

#define RHR(dev) (ioaddr[dev] + 0)  /* Receive Holding Buffer */
#define THR(dev) (ioaddr[dev] + 0)  /* Transmit Holding Buffer */
#define DLL(dev) (ioaddr[dev] + 0)  /* Divisor Latch LSB */
#define IER(dev) (ioaddr[dev] + 1)  /* Interrupt Enable Register */
#define DLM(dev) (ioaddr[dev] + 1)  /* Divisor Latch MSB */
#define IIR(dev) (ioaddr[dev] + 2)  /* Interrupt Id Register */
#define FCR(dev) (ioaddr[dev] + 2)  /* Fifo Control Register */
#define LCR(dev) (ioaddr[dev] + 3)  /* Line Control Register */
#define MCR(dev) (ioaddr[dev] + 4)  /* Modem Control Register */
#define LSR(dev) (ioaddr[dev] + 5)  /* Line Status Register */
#define MSR(dev) (ioaddr[dev] + 6)  /* Modem Status Register */


struct rt_16550_context {
    struct rtser_config     config;

    rtdm_irq_t              irq_handle;
    rtdm_lock_t             lock;

    int                     dev_id;

    int                     in_head;
    int                     in_tail;
    volatile size_t         in_npend;
    int                     in_nwait;
    rtdm_event_t            in_event;
    char                    in_buf[IN_BUFFER_SIZE];
    volatile unsigned long  in_lock;
    uint64_t                *in_history;

    int                     out_head;
    int                     out_tail;
    size_t                  out_npend;
    rtdm_event_t            out_event;
    char                    out_buf[OUT_BUFFER_SIZE];
    rtdm_mutex_t            out_lock;

    uint64_t                last_timestamp;
    volatile int            ioc_events;
    rtdm_event_t            ioc_event;
    volatile unsigned long  ioc_event_lock;

    int                     ier_status;
    int                     mcr_status;
    int                     status;
};


static const struct rtser_config default_config = {
    0xFFFF, RTSER_DEF_BAUD, RTSER_DEF_PARITY, RTSER_DEF_BITS,
    RTSER_DEF_STOPB, RTSER_DEF_HAND, RTSER_DEF_FIFO_DEPTH, RTSER_DEF_TIMEOUT,
    RTSER_DEF_TIMEOUT, RTSER_DEF_TIMEOUT, RTSER_DEF_TIMESTAMP_HISTORY,
    RTSER_DEF_EVENT_MASK
};

static struct rtdm_device   *device[MAX_DEVICES];

static unsigned long        ioaddr[MAX_DEVICES];
static int                  ioaddr_c;
static unsigned int         irq[MAX_DEVICES];
static int                  irq_c;
static unsigned int         baud_base[MAX_DEVICES];
static int                  baud_base_c;
static int                  tx_fifo[MAX_DEVICES];
static int                  tx_fifo_c;
static unsigned int         start_index;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param_array(ioaddr, ulong, &ioaddr_c, 0400);
MODULE_PARM_DESC(ioaddr, "I/O addresses of the serial devices");
module_param_array(irq, uint, &irq_c, 0400);
MODULE_PARM_DESC(irq, "IRQ numbers of the serial devices");
module_param_array(baud_base, uint, &baud_base_c, 0400);
MODULE_PARM_DESC(baud_base,
    "Maximum baud rate of the serial device (internal clock rate / 16)");
module_param_array(tx_fifo, int, &tx_fifo_c, 0400);
MODULE_PARM_DESC(tx_fifo, "Transmitter FIFO size");
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */
module_param(start_index, uint, 0400);
MODULE_PARM_DESC(start_index, "First device instance number to be used");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jan.kiszka@web.de");


static inline int rt_16550_rx_interrupt(struct rt_16550_context *ctx,
                                        uint64_t *timestamp)
{
    int dev_id = ctx->dev_id;
    int rbytes = 0;
    int lsr    = 0;
    int c;


    do {
        c = inb(RHR(dev_id));   /* read input character */

        ctx->in_buf[ctx->in_tail] = c;
        if (ctx->in_history)
            ctx->in_history[ctx->in_tail] = *timestamp;
        ctx->in_tail = (ctx->in_tail + 1) & (IN_BUFFER_SIZE - 1);

        if (++ctx->in_npend > IN_BUFFER_SIZE) {
            /*DBGIF(
                if (!testbits(ctx->status, RTSER_SOFT_OVERRUN_ERR))
                    rtdm_printk("%s: software buffer overrun!\n",
                                device[dev_id]->device_name);
                );*/
            lsr |= RTSER_SOFT_OVERRUN_ERR;
            ctx->in_npend--;
        }

        rbytes++;
        lsr &= ~RTSER_LSR_DATA;
        lsr |= (inb(LSR(dev_id)) &
                (RTSER_LSR_DATA | RTSER_LSR_OVERRUN_ERR |
                 RTSER_LSR_PARITY_ERR | RTSER_LSR_FRAMING_ERR |
                 RTSER_LSR_BREAK_IND));
    } while (testbits(lsr, RTSER_LSR_DATA));

    /* save new errors */
    ctx->status |= lsr;

    /* If we are enforcing the RTSCTS control flow and the input
       buffer is busy above the specified high watermark, clear
       RTS. */
/*    if (uart->i_count >= uart->config.rts_hiwm &&
        (uart->config.handshake & RT_UART_RTSCTS) != 0 &&
        (uart->modem & MCR_RTS) != 0) {
        uart->modem &= ~MCR_RTS;
        outb(uart->modem,MCR(uart));
    }*/

    return rbytes;
}


static inline void rt_16550_tx_interrupt(struct rt_16550_context *ctx)
{
    int c;
    int count;
    int dev_id = ctx->dev_id;


/*    if (uart->modem & MSR_CTS)*/
    {
        for (count = tx_fifo[dev_id];
             (count > 0) && (ctx->out_npend > 0);
             count--, ctx->out_npend--) {
            c = ctx->out_buf[ctx->out_head++];
            outb(c, THR(dev_id));
            ctx->out_head &= (OUT_BUFFER_SIZE - 1);
        }
    }
}


static inline void rt_16550_stat_interrupt(struct rt_16550_context *ctx)
{
    ctx->status |= (inb(LSR(ctx->dev_id)) &
                    (RTSER_LSR_OVERRUN_ERR | RTSER_LSR_PARITY_ERR |
                     RTSER_LSR_FRAMING_ERR |RTSER_LSR_BREAK_IND));
}


static int rt_16550_interrupt(rtdm_irq_t *irq_context)
{
    struct rt_16550_context *ctx;
    int                     dev_id;
    int                     iir;
    uint64_t                timestamp = rtdm_clock_read();
    int                     rbytes = 0;
    int                     events = 0;
    int                     modem;
    int                     ret = RTDM_IRQ_PROPAGATE;


    ctx = rtdm_irq_get_arg(irq_context, struct rt_16550_context);
    dev_id    = ctx->dev_id;

    rtdm_lock_get(&ctx->lock);

    while (((iir = inb(IIR(dev_id))) & IIR_PIRQ) == 0) {
        if (testbits(iir, IIR_RX | IIR_TMO)) {
            rbytes += rt_16550_rx_interrupt(ctx, &timestamp);
            events |= RTSER_EVENT_RXPEND;
        }

        if (testbits(iir, IIR_STAT))
            rt_16550_stat_interrupt(ctx);

        if (testbits(iir, IIR_TX))
            rt_16550_tx_interrupt(ctx);

        if (testbits(iir, IIR_MODEM)) {
            modem = inb(MSR(dev_id));
            if (modem & (modem << 4))
                events |= RTSER_EVENT_MODEMHI;
            if ((modem ^ 0xF0) & (modem << 4))
                events |= RTSER_EVENT_MODEMLO;
        }

        ret = RTDM_IRQ_ENABLE;
    }

    if (ctx->in_nwait > 0) {
        if ((ctx->in_nwait <= rbytes) || ctx->status) {
            ctx->in_nwait = 0;
            rtdm_event_signal(&ctx->in_event);
        }
        else
            ctx->in_nwait -= rbytes;
    }

    if (ctx->status) {
        events |= RTSER_EVENT_ERRPEND;
        ctx->ier_status &= ~IER_STAT;
    }

    if (testbits(events, ctx->config.event_mask)) {
        int old_events = ctx->ioc_events;

        ctx->last_timestamp = timestamp;
        ctx->ioc_events     = events;

        if (!old_events)
            rtdm_event_signal(&ctx->ioc_event);
    }

    if (testbits(ctx->ier_status, IER_TX) &&
        (ctx->out_npend == 0)) {
        /* mask transmitter empty interrupt */
        ctx->ier_status &= ~IER_TX;

        rtdm_event_signal(&ctx->out_event);
    }

    /* update interrupt mask */
    outb(ctx->ier_status, IER(dev_id));

    rtdm_lock_put(&ctx->lock);

    return ret;
}


static int rt_16550_set_config(struct rt_16550_context *ctx,
                               const struct rtser_config *config,
                               uint64_t **in_history_ptr)
{
    rtdm_lockctx_t  lock_ctx;
    int             dev_id;
    int             ret = 0;
    int             baud_div = 0;


    dev_id = ctx->dev_id;

    /* make line configuration atomic and IRQ-safe */
    rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

    if (testbits(config->config_mask, RTSER_SET_BAUD)) {
        ctx->config.baud_rate = config->baud_rate;
        baud_div = (baud_base[dev_id] + (ctx->config.baud_rate >> 1)) /
                   ctx->config.baud_rate;
        outb(LCR_DLAB,        LCR(dev_id));
        outb(baud_div & 0xff, DLL(dev_id));
        outb(baud_div >> 8,   DLM(dev_id));
    }

    if (testbits(config->config_mask, RTSER_SET_PARITY))
        ctx->config.parity    = config->parity & PARITY_MASK;
    if (testbits(config->config_mask, RTSER_SET_DATA_BITS))
        ctx->config.data_bits = config->data_bits & DATA_BITS_MASK;
    if (testbits(config->config_mask, RTSER_SET_STOP_BITS))
        ctx->config.stop_bits = config->stop_bits & STOP_BITS_MASK;

    if (testbits(config->config_mask, RTSER_SET_PARITY | RTSER_SET_DATA_BITS |
                                      RTSER_SET_STOP_BITS | RTSER_SET_BAUD)) {
        outb((ctx->config.parity << 3) | (ctx->config.stop_bits << 2) |
             ctx->config.data_bits, LCR(dev_id));
        ctx->status = 0;
        ctx->ioc_events &= ~RTSER_EVENT_ERRPEND;
    }

    if (testbits(config->config_mask, RTSER_SET_FIFO_DEPTH)) {
        ctx->config.fifo_depth = config->fifo_depth & FIFO_MASK;
        outb(FCR_FIFO | FCR_RESET,                    FCR(dev_id));
        outb(FCR_FIFO | ctx->config.fifo_depth, FCR(dev_id));
    }

    rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

    /* Timeout manipulation is not atomic. The user is supposed to take care
     * not to use and change timeouts at the same time. */
    if (testbits(config->config_mask, RTSER_SET_TIMEOUT_RX))
        ctx->config.rx_timeout = config->rx_timeout;
    if (testbits(config->config_mask, RTSER_SET_TIMEOUT_TX))
        ctx->config.tx_timeout = config->tx_timeout;
    if (testbits(config->config_mask, RTSER_SET_TIMEOUT_EVENT))
        ctx->config.event_timeout = config->event_timeout;

    if (testbits(config->config_mask, RTSER_SET_TIMESTAMP_HISTORY)) {
        /* change timestamp history atomically */
        rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

        if (testbits(config->timestamp_history, RTSER_RX_TIMESTAMP_HISTORY)) {
            if (!ctx->in_history) {
                ctx->in_history = *in_history_ptr;
                *in_history_ptr = NULL;
                if (!ctx->in_history)
                    ret = -ENOMEM;
            }
        } else {
            *in_history_ptr = ctx->in_history;
            ctx->in_history = NULL;
        }

        rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
    }

    if (testbits(config->config_mask, RTSER_SET_EVENT_MASK)) {
        /* change event mask atomically */
        rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

        ctx->config.event_mask = config->event_mask & EVENT_MASK;
        ctx->ioc_events = 0;

        if (testbits(config->event_mask, RTSER_EVENT_RXPEND) &&
            (ctx->in_npend > 0))
            ctx->ioc_events |= RTSER_EVENT_RXPEND;

        if (testbits(config->event_mask, RTSER_EVENT_ERRPEND) && ctx->status)
            ctx->ioc_events |= RTSER_EVENT_ERRPEND;

        if (testbits(config->event_mask,
                     RTSER_EVENT_MODEMHI | RTSER_EVENT_MODEMLO))
            /* enable modem status interrupt */
            ctx->ier_status |= IER_TX;
        else
            /* disable modem status interrupt */
            ctx->ier_status &= ~IER_TX;
        outb(ctx->ier_status, IER(ctx->dev_id));

        rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
    }

    if (testbits(config->config_mask, RTSER_SET_HANDSHAKE)) {
        /* change handshake atomically */
        rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

        ctx->config.handshake = config->handshake;
        switch (ctx->config.handshake) {
            case RTSER_RTSCTS_HAND:
                // ...?

            default: /* RTSER_NO_HAND */
                ctx->mcr_status =
                    RTSER_MCR_DTR | RTSER_MCR_RTS | RTSER_MCR_OUT2;
                break;
        }
        outb(ctx->mcr_status, MCR(dev_id));

        rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
    }

    return ret;
}


int rt_16550_open(struct rtdm_dev_context *context,
                  rtdm_user_info_t *user_info, int oflags)
{
    struct rt_16550_context *ctx;
    int                     dev_id = context->device->device_id;
    int                     ret;
    uint64_t                *dummy;


    ctx = (struct rt_16550_context *)context->dev_private;

    ret = rtdm_irq_request(&ctx->irq_handle, irq[dev_id], rt_16550_interrupt,
                           0, context->device->proc_name, ctx);
    if (ret < 0)
        return ret;

    /* IPC initialisation - cannot fail with used parameters */
    rtdm_lock_init(&ctx->lock);
    rtdm_event_init(&ctx->in_event, 0);
    rtdm_event_init(&ctx->out_event, 0);
    rtdm_event_init(&ctx->ioc_event, 0);
    rtdm_mutex_init(&ctx->out_lock);

    ctx->dev_id         = dev_id;

    ctx->in_head        = 0;
    ctx->in_tail        = 0;
    ctx->in_npend       = 0;
    ctx->in_nwait       = 0;
    ctx->in_lock        = 0;
    ctx->in_history     = NULL;

    ctx->out_head       = 0;
    ctx->out_tail       = 0;
    ctx->out_npend      = 0;

    ctx->ioc_events     = 0;
    ctx->ioc_event_lock = 0;
    ctx->status         = 0;

    rt_16550_set_config(ctx, &default_config, &dummy);

    /* enable IRQ interrupts */
    ctx->ier_status = IER_RX;
    outb(IER_RX, IER(dev_id));
    rtdm_irq_enable(&ctx->irq_handle);

    return 0;
}


int rt_16550_close(struct rtdm_dev_context *context,
                   rtdm_user_info_t *user_info)
{
    struct rt_16550_context *ctx;
    int                     dev_id;
    uint64_t                *in_history;
    rtdm_lockctx_t          lock_ctx;


    ctx    = (struct rt_16550_context *)context->dev_private;
    dev_id = ctx->dev_id;

    rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

    /* reset DTR and RTS */
    outb(0, MCR(dev_id));

    /* mask all UART interrupts and clear pending ones. */
    outb(0, IER(dev_id));
    inb(IIR(dev_id));
    inb(LSR(dev_id));
    inb(RHR(dev_id));
    inb(MSR(dev_id));

    in_history      = ctx->in_history;
    ctx->in_history = NULL;

    rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

    rtdm_irq_free(&ctx->irq_handle);

    rtdm_event_destroy(&ctx->in_event);
    rtdm_event_destroy(&ctx->out_event);
    rtdm_event_destroy(&ctx->ioc_event);

    rtdm_mutex_destroy(&ctx->out_lock);

    if (in_history) {
        if (test_bit(RTDM_CREATED_IN_NRT, &context->context_flags))
            kfree(in_history);
        else
            rtdm_free(in_history);
    }

    return 0;
}


int rt_16550_ioctl(struct rtdm_dev_context *context,
                   rtdm_user_info_t *user_info, int request, void *arg)
{
    struct rt_16550_context *ctx;
    int                     ret = 0;
    int                     dev_id = context->device->device_id;


    ctx = (struct rt_16550_context *)context->dev_private;

    switch (request) {
        case RTSER_RTIOC_GET_CONFIG:
            if (user_info) {
                if (!rtdm_rw_user_ok(user_info, arg,
                                     sizeof(struct rtser_config)) ||
                    rtdm_copy_to_user(user_info, arg, &ctx->config,
                                      sizeof(struct rtser_config)))
                    return -EFAULT;
            } else
                memcpy(arg, &ctx->config, sizeof(struct rtser_config));
            break;


        case RTSER_RTIOC_SET_CONFIG: {
            struct rtser_config *config;
            struct rtser_config config_buf;
            uint64_t            *hist_buf = NULL;

            config = (struct rtser_config *)arg;

            if (user_info) {

                if (!rtdm_read_user_ok(user_info, arg,
                                       sizeof(struct rtser_config)) ||
                    rtdm_copy_from_user(user_info, &config_buf, arg,
                                        sizeof(struct rtser_config)))
                    return -EFAULT;

                config = &config_buf;
            }

            if (testbits(config->config_mask, RTSER_SET_BAUD) &&
                (config->baud_rate > baud_base[dev_id])) {
                /* the baudrate is to high for this port */
                return -EINVAL;
            }

            if (testbits(config->config_mask, RTSER_SET_TIMESTAMP_HISTORY)) {
                if (test_bit(RTDM_CREATED_IN_NRT, &context->context_flags) &&
                    rtdm_in_rt_context()) {
                    /* already fail if we MAY allocate or release a non-RT
                     * buffer in RT context */
                    return -EPERM;
                }

                if (testbits(config->timestamp_history,
                             RTSER_RX_TIMESTAMP_HISTORY)) {
                    if (test_bit(RTDM_CREATED_IN_NRT,
                                 &context->context_flags))
                        hist_buf = kmalloc(IN_BUFFER_SIZE * sizeof(uint64_t),
                                           GFP_KERNEL);
                    else
                        hist_buf =
                            rtdm_malloc(IN_BUFFER_SIZE * sizeof(uint64_t));
                }

                if (!hist_buf)
                    return -ENOMEM;
            }

            rt_16550_set_config(ctx, config, &hist_buf);

            if (hist_buf) {
                if (test_bit(RTDM_CREATED_IN_NRT, &context->context_flags))
                    kfree(hist_buf);
                else
                    rtdm_free(hist_buf);
            }

            break;
        }

        case RTSER_RTIOC_GET_STATUS: {
            rtdm_lockctx_t lock_ctx;

            if (user_info) {
                struct rtser_status status_buf;

                status_buf.line_status  = inb(LSR(ctx->dev_id));
                status_buf.modem_status = inb(MSR(ctx->dev_id));

                if (!rtdm_rw_user_ok(user_info, arg,
                                     sizeof(struct rtser_status)) ||
                    rtdm_copy_to_user(user_info, arg, &status_buf,
                                      sizeof(struct rtser_status)))
                    return -EFAULT;
            } else {
                ((struct rtser_status *)arg)->line_status  =
                    inb(LSR(ctx->dev_id));
                ((struct rtser_status *)arg)->modem_status =
                    inb(MSR(ctx->dev_id));
            }
            rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);
            ctx->status = 0;
            ctx->ioc_events &= ~RTSER_EVENT_ERRPEND;
            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
            break;
        }

        case RTSER_RTIOC_GET_CONTROL:
            if (user_info) {
                if (!rtdm_rw_user_ok(user_info, arg, sizeof(int)) ||
                    rtdm_copy_to_user(user_info, arg, &ctx->mcr_status,
                                      sizeof(int)))
                    ret = -EFAULT;
            } else
                *(int *)arg = ctx->mcr_status;

            break;


        case RTSER_RTIOC_SET_CONTROL: {
            int             new_mcr;
            rtdm_lockctx_t  lock_ctx;

            new_mcr = (int)arg;

            rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);
            ctx->mcr_status = new_mcr;
            outb(new_mcr, MCR(ctx->dev_id));
            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
            break;
        }

        case RTSER_RTIOC_WAIT_EVENT: {
            struct rtser_event  ev = { rxpend_timestamp: 0 };
            rtdm_lockctx_t      lock_ctx;
            rtdm_toseq_t        timeout_seq;

            if (!rtdm_in_rt_context())
                return -ENOSYS;

            /* only one waiter allowed, stop any further attempts here */
            if (test_and_set_bit(0, &ctx->ioc_event_lock))
                return -EBUSY;

            rtdm_toseq_init(&timeout_seq, ctx->config.event_timeout);

            rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

            while (!ctx->ioc_events) {
                /* enable error interrupt only when the user waits for it */
                if (testbits(ctx->config.event_mask, RTSER_EVENT_ERRPEND)) {
                    ctx->ier_status |= IER_STAT;
                    outb(ctx->ier_status, IER(ctx->dev_id));
                }

                rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

                ret = rtdm_event_timedwait(&ctx->ioc_event,
                                           ctx->config.event_timeout,
                                           &timeout_seq);
                if (ret < 0) {
                    clear_bit(0, &ctx->ioc_event_lock);
                    if (ret == -EIDRM)  /* device has been closed */
                        ret = -EBADF;
                    goto wait_unlock_out;
                }

                rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);
            }

            ev.events = ctx->ioc_events;
            ctx->ioc_events &= ~(RTSER_EVENT_MODEMHI | RTSER_EVENT_MODEMLO);

            ev.last_timestamp = ctx->last_timestamp;
            ev.rx_pending     = ctx->in_npend;

            if (ctx->in_history)
                ev.rxpend_timestamp = ctx->in_history[ctx->in_head];

            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

            if (user_info) {
                if (!rtdm_rw_user_ok(user_info, arg,
                                     sizeof(struct rtser_event)) ||
                    rtdm_copy_to_user(user_info, arg, &ev,
                                      sizeof(struct rtser_event)))
                    ret = -EFAULT;
            } else
                memcpy(arg, &ev, sizeof(struct rtser_event));

          wait_unlock_out:
            /* release the simple event waiter lock */
            clear_bit(0, &ctx->ioc_event_lock);
            break;
        }

        default:
            ret = -ENOTTY;
    }

    return ret;
}


int rt_16550_read(struct rtdm_dev_context *context,
                  rtdm_user_info_t *user_info, void *buf, size_t nbyte)
{
    struct rt_16550_context *ctx;
    int                     dev_id;
    rtdm_lockctx_t          lock_ctx;
    size_t                  read = 0;
    int                     pending;
    int                     block;
    int                     subblock;
    int                     in_pos;
    char                    *out_pos = (char *)buf;
    rtdm_toseq_t            timeout_seq;
    int                     ret = -EAGAIN;  /* for non-blocking read */
    int                     nonblocking;


    if (nbyte == 0)
        return 0;

    if (user_info && !rtdm_rw_user_ok(user_info, buf, nbyte))
        return -EFAULT;

    ctx    = (struct rt_16550_context *)context->dev_private;
    dev_id = ctx->dev_id;

    rtdm_toseq_init(&timeout_seq, ctx->config.rx_timeout);

    /* non-blocking is handled separately here */
    nonblocking = (ctx->config.rx_timeout < 0);

    /* only one reader allowed, stop any further attempts here */
    if (test_and_set_bit(0, &ctx->in_lock))
        return -EBUSY;

    while (nbyte > 0) {
        rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

        /* switch on error interrupt - the user is ready to listen */
        if (!testbits(ctx->ier_status, IER_STAT)) {
            ctx->ier_status |= IER_STAT;
            outb(ctx->ier_status, IER(ctx->dev_id));
        }

        if (ctx->status) {
            if (testbits(ctx->status, RTSER_LSR_BREAK_IND))
                ret = -EPIPE;
            else
                ret = -EIO;
            ctx->status = 0;
            break;
        }

        pending = ctx->in_npend;

        if (pending > 0) {
            block = subblock = (pending <= nbyte) ? pending : nbyte;
            in_pos = ctx->in_head;

            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

            /* do we have to wrap around the buffer end? */
            if (in_pos + subblock > IN_BUFFER_SIZE) {
                /* treat the block between head and buffer end separately */
                subblock = IN_BUFFER_SIZE - in_pos;

                if (user_info) {
                    if (rtdm_copy_to_user(user_info, out_pos,
                                &ctx->in_buf[in_pos], subblock) != 0) {
                        ret = -EFAULT;
                        break;
                    }
                } else
                    memcpy(out_pos, &ctx->in_buf[in_pos], subblock);

                read    += subblock;
                out_pos += subblock;

                subblock = block - subblock;
                in_pos   = 0;
            }

            if (user_info) {
                if (rtdm_copy_to_user(user_info, out_pos,
                                &ctx->in_buf[in_pos], subblock) != 0) {
                    ret = -EFAULT;
                    break;
                }
            } else
                memcpy(out_pos, &ctx->in_buf[in_pos], subblock);

            read    += subblock;
            out_pos += subblock;
            nbyte   -= block;

            rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

            ctx->in_head = (ctx->in_head + block) & (IN_BUFFER_SIZE - 1);
            if ((ctx->in_npend -= block) == 0)
                ctx->ioc_events &= ~RTSER_EVENT_RXPEND;

            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
            continue;
        }

        if (nonblocking) {
            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
            /* ret was set to EAGAIN in case of real non-blocking call or
               contains the error returned by rtdm_event_wait[_until] */
            break;
        }

        ctx->in_nwait = nbyte;

        rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

        ret = rtdm_event_timedwait(&ctx->in_event, ctx->config.rx_timeout,
                                   &timeout_seq);
        if (ret < 0) {
            if (ret == -EIDRM) {
                /* device has been closed - return immediately */
                return -EBADF;
            }

            nonblocking = 1;
            if (ctx->in_npend > 0)
                continue; /* final turn: collect pending bytes before exit */

            ctx->in_nwait = 0;
            break;
        }
    }

    /* release the simple reader lock */
    clear_bit(0, &ctx->in_lock);

    if ((read > 0) && ((ret == 0) || (ret == -EAGAIN) ||
                       (ret == -ETIMEDOUT) || (ret == -EINTR)))
        ret = read;

    return ret;
}


int rt_16550_write(struct rtdm_dev_context *context,
                   rtdm_user_info_t *user_info, const void *buf, size_t nbyte)
{
    struct rt_16550_context *ctx;
    int                     dev_id;
    rtdm_lockctx_t          lock_ctx;
    size_t                  written = 0;
    int                     free;
    int                     block;
    int                     subblock;
    int                     out_pos;
    char                    *in_pos = (char *)buf;
    rtdm_toseq_t            timeout_seq;
    int                     ret;


    if (nbyte == 0)
        return 0;

    if (user_info &&
        !rtdm_read_user_ok(user_info, buf, nbyte))
        return -EFAULT;

    ctx    = (struct rt_16550_context *)context->dev_private;
    dev_id = ctx->dev_id;

    rtdm_toseq_init(&timeout_seq, ctx->config.rx_timeout);

    /* make write operation atomic */
    ret = rtdm_mutex_timedlock(&ctx->out_lock, ctx->config.rx_timeout,
                               &timeout_seq);
    if (ret)
        return ret;

    while (nbyte > 0) {
        rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

        free = OUT_BUFFER_SIZE - ctx->out_npend;

        if (free > 0) {
            block = subblock = (nbyte <= free) ? nbyte : free;
            out_pos = ctx->out_tail;

            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

            /* do we have to wrap around the buffer end? */
            if (out_pos + subblock > OUT_BUFFER_SIZE) {
                /* treat the block between head and buffer end separately */
                subblock = OUT_BUFFER_SIZE - out_pos;

                if (user_info) {
                    if (rtdm_copy_from_user(user_info, &ctx->out_buf[out_pos],
                                            in_pos, subblock) != 0) {
                        ret = -EFAULT;
                        break;
                    }
                } else
                    memcpy(&ctx->out_buf[out_pos], in_pos, subblock);

                written += subblock;
                in_pos  += subblock;

                subblock = block - subblock;
                out_pos  = 0;
            }

            if (user_info) {
                if (rtdm_copy_from_user(user_info, &ctx->out_buf[out_pos],
                                        in_pos, subblock) != 0) {
                    ret = -EFAULT;
                    break;
                }
            } else
                memcpy(&ctx->out_buf[out_pos], in_pos, block);

            written += subblock;
            in_pos  += subblock;
            nbyte   -= block;

            rtdm_lock_get_irqsave(&ctx->lock, lock_ctx);

            ctx->out_tail = (ctx->out_tail + block) & (OUT_BUFFER_SIZE - 1);
            ctx->out_npend += block;

            /* unmask tx interrupt */
            ctx->ier_status |= IER_TX;
            outb(ctx->ier_status, IER(dev_id));

            rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);
            continue;
        }

        rtdm_lock_put_irqrestore(&ctx->lock, lock_ctx);

        ret = rtdm_event_timedwait(&ctx->out_event, ctx->config.tx_timeout,
                                   &timeout_seq);
        if (ret < 0) {
            if (ret == -EIDRM) {
                /* device has been closed - return immediately */
                return -EBADF;
            }
            if (ret == -EWOULDBLOCK) {
                /* fix error code for non-blocking mode */
                ret = -EAGAIN;
            }
            break;
        }
    }

    rtdm_mutex_unlock(&ctx->out_lock);

    if ((written > 0) && ((ret == 0) || (ret == -EAGAIN) ||
                          (ret == -ETIMEDOUT) || (ret == -EINTR)))
        ret = written;

    return ret;
}


static const struct rtdm_device __initdata device_tmpl = {
    struct_version:     RTDM_DEVICE_STRUCT_VER,

    device_flags:       RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,
    context_size:       sizeof(struct rt_16550_context),
    device_name:        "",

    open_rt:            rt_16550_open,
    open_nrt:           rt_16550_open,

    ops: {
        close_rt:       rt_16550_close,
        close_nrt:      rt_16550_close,

        ioctl_rt:       rt_16550_ioctl,
        ioctl_nrt:      rt_16550_ioctl,

        read_rt:        rt_16550_read,
        read_nrt:       NULL,

        write_rt:       rt_16550_write,
        write_nrt:      NULL,

        recvmsg_rt:     NULL,
        recvmsg_nrt:    NULL,

        sendmsg_rt:     NULL,
        sendmsg_nrt:    NULL,
    },

    device_class:       RTDM_CLASS_SERIAL,
    device_sub_class:   RTDM_SUBCLASS_16550A,
    driver_name:        "rt_16550A",
    driver_version:     RTDM_DRIVER_VER(1, 2, 0),
    peripheral_name:    "UART 16550A",
    provider_name:      "Jan Kiszka",
};

void cleanup_module(void);

int __init init_module(void)
{
    struct rtdm_device  *dev;
    int                 ret;
    int                 i;


    if (irq_c < ioaddr_c)
        return -EINVAL;

    for (i = 0; i < ioaddr_c; i++) {
        dev = kmalloc(sizeof(struct rtdm_device), GFP_KERNEL);
        ret = -ENOMEM;
        if (!dev)
            goto cleanup_out;

        memcpy(dev, &device_tmpl, sizeof(struct rtdm_device));
        snprintf(dev->device_name, RTDM_MAX_DEVNAME_LEN, "rtser%d",
                 start_index+i);
        dev->device_id = i;

        dev->proc_name = dev->device_name;

        ret = -EBUSY;
        if (!request_region(ioaddr[i], 8, dev->device_name))
            goto kfree_out;

        if (baud_base[i] == 0)
            baud_base[i] = DEFAULT_BAUD_BASE;

        if (tx_fifo[i] == 0)
            tx_fifo[i] = DEFAULT_TX_FIFO;

        /* Mask all UART interrupts and clear pending ones. */
        outb(0, IER(i));
        inb(IIR(i));
        inb(LSR(i));
        inb(RHR(i));
        inb(MSR(i));

        ret = rtdm_dev_register(dev);

        if (ret < 0)
            goto rel_region_out;

        device[i] = dev;
    }

    return 0;


 rel_region_out:
    release_region(ioaddr[i], 8);

 kfree_out:
    kfree(dev);

 cleanup_out:
    cleanup_module();

    return ret;
}


void cleanup_module(void)
{
    int i;


    for (i = 0; i < MAX_DEVICES; i++)
        if (device[i]) {
            rtdm_dev_unregister(device[i], 1000);
            release_region(ioaddr[i], 8);
            kfree(device[i]);
        }
}
