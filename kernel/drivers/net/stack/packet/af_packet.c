/***
 *
 *  packet/af_packet.c
 *
 *  RTnet - real-time networking subsystem
 *  Copyright (C) 2003-2006 Jan Kiszka <jan.kiszka@web.de>
 *  Copyright (C) 2006 Jorge Almeida <j-almeida@criticalsoftware.com>
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

#include <linux/module.h>
#include <linux/sched.h>

#include <rtnet_iovec.h>
#include <rtnet_socket.h>
#include <stack_mgr.h>

MODULE_LICENSE("GPL");


/***
 *  rt_packet_rcv
 */
static int rt_packet_rcv(struct rtskb *skb, struct rtpacket_type *pt)
{
    struct rtsocket *sock   = container_of(pt, struct rtsocket,
                                           prot.packet.packet_type);
    int             ifindex = sock->prot.packet.ifindex;
    void            (*callback_func)(struct rtdm_dev_context *, void *);
    void            *callback_arg;
    rtdm_lockctx_t  context;


    if (unlikely((ifindex != 0) && (ifindex != skb->rtdev->ifindex)))
        return -EUNATCH;

#ifdef CONFIG_XENO_DRIVERS_NET_ETH_P_ALL
    if (pt->type == htons(ETH_P_ALL)) {
        struct rtskb *clone_skb = rtskb_clone(skb, &sock->skb_pool);
        if (clone_skb == NULL)
            return 0;
        skb = clone_skb;
    } else
#endif /* CONFIG_XENO_DRIVERS_NET_ETH_P_ALL */
        if (unlikely(rtskb_acquire(skb, &sock->skb_pool) < 0)) {
            kfree_rtskb(skb);
            return 0;
        }

    rtdev_reference(skb->rtdev);
    rtskb_queue_tail(&sock->incoming, skb);
    rtdm_sem_up(&sock->pending_sem);

    rtdm_lock_get_irqsave(&sock->param_lock, context);
    callback_func = sock->callback_func;
    callback_arg  = sock->callback_arg;
    rtdm_lock_put_irqrestore(&sock->param_lock, context);

    if (callback_func)
        callback_func(rt_socket_context(sock), callback_arg);

    return 0;
}



/***
 *  rt_packet_bind
 */
static int rt_packet_bind(struct rtsocket *sock, const struct sockaddr *addr,
                          socklen_t addrlen)
{
    struct sockaddr_ll      *sll = (struct sockaddr_ll *)addr;
    struct rtpacket_type    *pt  = &sock->prot.packet.packet_type;
    int                     new_type;
    int                     ret;
    rtdm_lockctx_t          context;


    if ((addrlen < (int)sizeof(struct sockaddr_ll)) ||
        (sll->sll_family != AF_PACKET))
        return -EINVAL;

    new_type = (sll->sll_protocol != 0) ? sll->sll_protocol : sock->protocol;

    rtdm_lock_get_irqsave(&sock->param_lock, context);

    /* release existing binding */
    if ((pt->type != 0) && ((ret = rtdev_remove_pack(pt)) < 0)) {
        rtdm_lock_put_irqrestore(&sock->param_lock, context);
        return ret;
    }

    pt->type = new_type;
    sock->prot.packet.ifindex = sll->sll_ifindex;

    /* if protocol is non-zero, register the packet type */
    if (new_type != 0) {
        pt->handler     = rt_packet_rcv;
        pt->err_handler = NULL;

        ret = rtdev_add_pack(pt);
    } else
        ret = 0;

    rtdm_lock_put_irqrestore(&sock->param_lock, context);

    return ret;
}



/***
 *  rt_packet_getsockname
 */
static int rt_packet_getsockname(struct rtsocket *sock, struct sockaddr *addr,
                                 socklen_t *addrlen)
{
    struct sockaddr_ll  *sll = (struct sockaddr_ll*)addr;
    struct rtnet_device *rtdev;
    rtdm_lockctx_t      context;


    if (*addrlen < sizeof(struct sockaddr_ll))
        return -EINVAL;

    rtdm_lock_get_irqsave(&sock->param_lock, context);

    sll->sll_family   = AF_PACKET;
    sll->sll_ifindex  = sock->prot.packet.ifindex;
    sll->sll_protocol = sock->protocol;

    rtdm_lock_put_irqrestore(&sock->param_lock, context);

    rtdev = rtdev_get_by_index(sll->sll_ifindex);
    if (rtdev != NULL) {
        sll->sll_hatype = rtdev->type;
        sll->sll_halen  = rtdev->addr_len;

        memcpy(sll->sll_addr, rtdev->dev_addr, rtdev->addr_len);

        rtdev_dereference(rtdev);
    } else {
        sll->sll_hatype = 0;
        sll->sll_halen  = 0;
    }

    *addrlen = sizeof(struct sockaddr_ll);

    return 0;
}



/***
 * rt_packet_socket - initialize a packet socket
 */
static int rt_packet_socket(struct rtdm_dev_context *sockctx,
                            rtdm_user_info_t *user_info, int protocol)
{
    struct rtsocket *sock = (struct rtsocket *)&sockctx->dev_private;
    int             ret;


    if ((ret = rt_socket_init(sockctx, protocol)) != 0)
        return ret;

    sock->prot.packet.packet_type.type = protocol;
    sock->prot.packet.ifindex          = 0;

    /* if protocol is non-zero, register the packet type */
    if (protocol != 0) {
        sock->prot.packet.packet_type.handler     = rt_packet_rcv;
        sock->prot.packet.packet_type.err_handler = NULL;

        if ((ret = rtdev_add_pack(&sock->prot.packet.packet_type)) < 0) {
            rt_socket_cleanup(sockctx);
            return ret;
        }
    }

    return 0;
}



/***
 *  rt_packet_close
 */
static int rt_packet_close(struct rtdm_dev_context *sockctx,
                           rtdm_user_info_t *user_info)
{
    struct rtsocket         *sock = (struct rtsocket *)&sockctx->dev_private;
    struct rtpacket_type    *pt = &sock->prot.packet.packet_type;
    struct rtskb            *del;
    int                     ret = 0;
    rtdm_lockctx_t          context;


    rtdm_lock_get_irqsave(&sock->param_lock, context);

    if ((pt->type != 0) && ((ret = rtdev_remove_pack(pt)) == 0))
        pt->type = 0;

    rtdm_lock_put_irqrestore(&sock->param_lock, context);

    /* free packets in incoming queue */
    while ((del = rtskb_dequeue(&sock->incoming)) != NULL) {
        rtdev_dereference(del->rtdev);
        kfree_rtskb(del);
    }

    if (ret == 0)
        ret = rt_socket_cleanup(sockctx);

    return ret;
}



/***
 *  rt_packet_ioctl
 */
static int rt_packet_ioctl(struct rtdm_dev_context *sockctx,
                           rtdm_user_info_t *user_info,
                           unsigned int request, void *arg)
{
    struct rtsocket *sock = (struct rtsocket *)&sockctx->dev_private;
    struct _rtdm_setsockaddr_args *setaddr = arg;
    struct _rtdm_getsockaddr_args *getaddr = arg;


    /* fast path for common socket IOCTLs */
    if (_IOC_TYPE(request) == RTIOC_TYPE_NETWORK)
        return rt_socket_common_ioctl(sockctx, user_info, request, arg);

    switch (request) {
        case _RTIOC_BIND:
            return rt_packet_bind(sock, setaddr->addr, setaddr->addrlen);

        case _RTIOC_GETSOCKNAME:
            return rt_packet_getsockname(sock, getaddr->addr,
                                         getaddr->addrlen);

        default:
            return rt_socket_if_ioctl(sockctx, user_info, request, arg);
    }
}



/***
 *  rt_packet_recvmsg
 */
static ssize_t rt_packet_recvmsg(struct rtdm_dev_context *sockctx,
                                 rtdm_user_info_t *user_info,
                                 struct msghdr *msg, int msg_flags)
{
    struct rtsocket     *sock = (struct rtsocket *)&sockctx->dev_private;
    size_t              len   = rt_iovec_len(msg->msg_iov, msg->msg_iovlen);
    size_t              copy_len;
    size_t              real_len;
    struct rtskb        *rtskb;
    struct sockaddr_ll  *sll;
    int                 ret;
    nanosecs_rel_t      timeout = sock->timeout;


    /* non-blocking receive? */
    if (testbits(msg_flags, MSG_DONTWAIT))
        timeout = -1;

    ret = rtdm_sem_timeddown(&sock->pending_sem, timeout, NULL);
    if (unlikely(ret < 0))
        switch (ret) {
            case -EWOULDBLOCK:
            case -ETIMEDOUT:
            case -EINTR:
                return ret;

            default:
                return -EBADF;   /* socket has been closed */
        }

    rtskb = rtskb_dequeue_chain(&sock->incoming);
    RTNET_ASSERT(rtskb != NULL, return -EFAULT;);

    sll = msg->msg_name;

    /* copy the address */
    msg->msg_namelen = sizeof(*sll);
    if (sll != NULL) {
        struct rtnet_device *rtdev = rtskb->rtdev;

        sll->sll_family   = AF_PACKET;
        sll->sll_hatype   = rtdev->type;
        sll->sll_protocol = rtskb->protocol;
        sll->sll_pkttype  = rtskb->pkt_type;
        sll->sll_ifindex  = rtdev->ifindex;

        /* Ethernet specific - we rather need some parse handler here */
        memcpy(sll->sll_addr, rtskb->mac.ethernet->h_source, ETH_ALEN);
        sll->sll_halen = ETH_ALEN;
    }

    /* Include the header in raw delivery */
    if (sockctx->device->socket_type != SOCK_DGRAM)
        rtskb_push(rtskb, rtskb->data - rtskb->mac.raw);

    copy_len = real_len = rtskb->len;

    /* The data must not be longer than the available buffer size */
    if (copy_len > len) {
        copy_len = len;
        msg->msg_flags |= MSG_TRUNC;
    }

    rt_memcpy_tokerneliovec(msg->msg_iov, rtskb->data, copy_len);

    if ((msg_flags & MSG_PEEK) == 0) {
        rtdev_dereference(rtskb->rtdev);
        kfree_rtskb(rtskb);
    } else {
        rtskb_queue_head(&sock->incoming, rtskb);
        rtdm_sem_up(&sock->pending_sem);
    }

    return real_len;
}



/***
 *  rt_packet_sendmsg
 */
static ssize_t rt_packet_sendmsg(struct rtdm_dev_context *sockctx,
                                 rtdm_user_info_t *user_info,
                                 const struct msghdr *msg, int msg_flags)
{
    struct rtsocket     *sock = (struct rtsocket *)&sockctx->dev_private;
    size_t              len   = rt_iovec_len(msg->msg_iov, msg->msg_iovlen);
    struct sockaddr_ll  *sll  = (struct sockaddr_ll*)msg->msg_name;
    struct rtnet_device *rtdev;
    struct rtskb        *rtskb;
    unsigned short      proto;
    unsigned char       *addr;
    int                 ifindex;
    int                 ret = 0;


    if (msg_flags & MSG_OOB)    /* Mirror BSD error message compatibility */
        return -EOPNOTSUPP;
    if (msg_flags & ~MSG_DONTWAIT)
        return -EINVAL;

    if (sll == NULL) {
        /* Note: We do not care about races with rt_packet_bind here -
           the user has to do so. */
        ifindex = sock->prot.packet.ifindex;
        proto   = sock->prot.packet.packet_type.type;
        addr    = NULL;
    } else {
        if ((msg->msg_namelen < sizeof(struct sockaddr_ll)) ||
            (msg->msg_namelen <
                (sll->sll_halen + offsetof(struct sockaddr_ll, sll_addr))) ||
            ((sll->sll_family != AF_PACKET) &&
            (sll->sll_family != AF_UNSPEC)))
        return -EINVAL;

        ifindex = sll->sll_ifindex;
        proto   = sll->sll_protocol;
        addr    = sll->sll_addr;
    }

    if ((rtdev = rtdev_get_by_index(ifindex)) == NULL)
        return -ENODEV;

    rtskb = alloc_rtskb(rtdev->hard_header_len + len, &sock->skb_pool);
    if (rtskb == NULL) {
        ret = -ENOBUFS;
        goto out;
    }

    /* If an RTmac discipline is active, this becomes a pure sanity check to
       avoid writing beyond rtskb boundaries. The hard check is then performed
       upon rtdev_xmit() by the discipline's xmit handler. */
    if (len > rtdev->mtu + ((sockctx->device->socket_type == SOCK_RAW) ?
                            rtdev->hard_header_len : 0)) {
        ret = -EMSGSIZE;
        goto err;
    }

    if ((sll != NULL) && (sll->sll_halen != rtdev->addr_len)) {
        ret = -EINVAL;
        goto err;
    }

    rtskb_reserve(rtskb, rtdev->hard_header_len);

    rtskb->rtdev    = rtdev;
    rtskb->priority = sock->priority;

    if (rtdev->hard_header) {
        int hdr_len;

        ret = -EINVAL;
        hdr_len = rtdev->hard_header(rtskb, rtdev, ntohs(proto),
                                     addr, NULL, len);
        if (sockctx->device->socket_type != SOCK_DGRAM) {
            rtskb->tail = rtskb->data;
            rtskb->len = 0;
        } else if (hdr_len < 0)
            goto err;
    }

    rt_memcpy_fromkerneliovec(rtskb_put(rtskb, len), msg->msg_iov, len);

    if ((rtdev->flags & IFF_UP) != 0) {
        if ((ret = rtdev_xmit(rtskb)) == 0)
            ret = len;
    } else {
        ret = -ENETDOWN;
        goto err;
    }

 out:
    rtdev_dereference(rtdev);
    return ret;

 err:
    kfree_rtskb(rtskb);
    goto out;
}



static struct rtdm_device   packet_proto_dev = {
    .struct_version =   RTDM_DEVICE_STRUCT_VER,

    .device_flags =     RTDM_PROTOCOL_DEVICE,
    .context_size =     sizeof(struct rtsocket),

    .protocol_family =  PF_PACKET,
    .socket_type =      SOCK_DGRAM,

    .socket_nrt =       rt_packet_socket,

    .ops = {
        .close_nrt =    rt_packet_close,
        .ioctl_rt =     rt_packet_ioctl,
        .ioctl_nrt =    rt_packet_ioctl,
        .recvmsg_rt =   rt_packet_recvmsg,
        .sendmsg_rt =   rt_packet_sendmsg,
#ifdef CONFIG_XENO_DRIVERS_NET_SELECT_SUPPORT
        .select_bind =  rt_socket_select_bind,
#endif
    },

    .device_class =     RTDM_CLASS_NETWORK,
    .device_sub_class = RTDM_SUBCLASS_RTNET,
    .driver_name =      "rtpacket",
    .driver_version =   RTNET_RTDM_VER,
    .peripheral_name =  "Real-Time Packet Socket Interface",
    .provider_name =    rtnet_rtdm_provider_name,

    .proc_name =        "PACKET_DGRAM"
};


static struct rtdm_device   raw_packet_proto_dev = {
    .struct_version =   RTDM_DEVICE_STRUCT_VER,

    .device_flags =     RTDM_PROTOCOL_DEVICE,
    .context_size =     sizeof(struct rtsocket),

    .protocol_family =  PF_PACKET,
    .socket_type =      SOCK_RAW,

    .socket_nrt =       rt_packet_socket,

    .ops = {
        .close_nrt =    rt_packet_close,
        .ioctl_rt =     rt_packet_ioctl,
        .ioctl_nrt =    rt_packet_ioctl,
        .recvmsg_rt =   rt_packet_recvmsg,
        .sendmsg_rt =   rt_packet_sendmsg,
#ifdef CONFIG_XENO_DRIVERS_NET_SELECT_SUPPORT
        .select_bind =  rt_socket_select_bind,
#endif
    },

    .device_class =     RTDM_CLASS_NETWORK,
    .device_sub_class = RTDM_SUBCLASS_RTNET,
    .driver_name =      "rtpacket",
    .driver_version =   RTNET_RTDM_VER,
    .peripheral_name =  "Real-Time Packet Socket Interface",
    .provider_name =    rtnet_rtdm_provider_name,

    .proc_name =        "PACKET_RAW"
};


static int __init rt_packet_proto_init(void)
{
    int err;

    err = rtdm_dev_register(&packet_proto_dev);
    if (err)
        return err;

    err = rtdm_dev_register(&raw_packet_proto_dev);
    if (err)
        rtdm_dev_unregister(&packet_proto_dev, 1000);

    return err;
}


static void rt_packet_proto_release(void)
{
    rtdm_dev_unregister(&packet_proto_dev, 1000);
    rtdm_dev_unregister(&raw_packet_proto_dev, 1000);
}


module_init(rt_packet_proto_init);
module_exit(rt_packet_proto_release);



/**********************************************************
 * Utilities                                              *
 **********************************************************/

static int hex2int(unsigned char hex_char)
{
    if ((hex_char >= '0') && (hex_char <= '9'))
        return hex_char - '0';
    else if ((hex_char >= 'a') && (hex_char <= 'f'))
        return hex_char - 'a' + 10;
    else if ((hex_char >= 'A') && (hex_char <= 'F'))
        return hex_char - 'A' + 10;
    else
        return -EINVAL;
}



int rt_eth_aton(unsigned char *addr_buf, const char *mac)
{
    int i = 0;
    int nibble;


    while (1) {
        if (*mac == 0)
            return -EINVAL;

        if ((nibble = hex2int(*mac++)) < 0)
            return nibble;
        *addr_buf = nibble << 4;

        if (*mac == 0)
            return -EINVAL;

        if ((nibble = hex2int(*mac++)) < 0)
            return nibble;
        *addr_buf++ |= nibble;

        if (++i == 6)
            break;

        if ((*mac == 0) || (*mac++ != ':'))
            return -EINVAL;

    }
    return 0;
}

EXPORT_SYMBOL(rt_eth_aton);
