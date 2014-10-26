/***
 *
 *  include/rtnet_socket.h
 *
 *  RTnet - real-time networking subsystem
 *  Copyright (C) 1999       Lineo, Inc
 *                1999, 2002 David A. Schleef <ds@schleef.org>
 *                2002       Ulrich Marx <marx@kammer.uni-hannover.de>
 *                2003-2005  Jan Kiszka <jan.kiszka@web.de>
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

#ifndef __RTNET_SOCKET_H_
#define __RTNET_SOCKET_H_

#include <asm/atomic.h>
#include <linux/list.h>

#include <rtdev.h>
#include <rtnet.h>
#include <rtnet_sys.h>
#include <stack_mgr.h>

#include <rtdm/driver.h>


struct rtsocket {
    unsigned short          protocol;

    struct rtskb_queue      skb_pool;
    unsigned int            pool_size;
    struct mutex            pool_nrt_lock;

    struct rtskb_queue      incoming;

    rtdm_lock_t             param_lock;

    unsigned int            priority;
    nanosecs_rel_t          timeout;    /* receive timeout, 0 for infinite */

    rtdm_sem_t              pending_sem;

    void                    (*callback_func)(struct rtdm_dev_context *,
                                             void *arg);
    void                    *callback_arg;

    union {
        /* IP specific */
        struct {
            u32             saddr;      /* source ip-addr (bind) */
            u32             daddr;      /* destination ip-addr */
            u16             sport;      /* source port */
            u16             dport;      /* destination port */

            int             reg_index;  /* index in port registry */
            u8              tos;
            u8              state;
        } inet;

        /* packet socket specific */
        struct {
            struct rtpacket_type packet_type;
            int                  ifindex;
        } packet;
    } prot;
};


static inline struct rtdm_dev_context *rt_socket_context(struct rtsocket *sock)
{
    return container_of((void *)sock, struct rtdm_dev_context, dev_private);
}

#define rt_socket_reference(sock)   \
    atomic_inc(&(rt_socket_context(sock)->close_lock_count))
#define rt_socket_dereference(sock) \
    atomic_dec(&(rt_socket_context(sock)->close_lock_count))

int rt_socket_init(struct rtdm_dev_context *context, unsigned short protocol);
int rt_socket_cleanup(struct rtdm_dev_context *context);
int rt_socket_common_ioctl(struct rtdm_dev_context *context,
                           rtdm_user_info_t *user_info,
                           int request, void *arg);
int rt_socket_if_ioctl(struct rtdm_dev_context *context,
                       rtdm_user_info_t *user_info,
                       int request, void *arg);
#ifdef CONFIG_XENO_DRIVERS_NET_SELECT_SUPPORT
int rt_socket_select_bind(struct rtdm_dev_context *context,
                          rtdm_selector_t *selector,
                          enum rtdm_selecttype type,
                          unsigned fd_index);
#endif /* CONFIG_XENO_DRIVERS_NET_SELECT_SUPPORT */

int rt_bare_socket_init(struct rtsocket *sock, unsigned short protocol,
                        unsigned int priority, unsigned int pool_size);

static inline void rt_bare_socket_cleanup(struct rtsocket *sock)
{
    rtskb_pool_release(&sock->skb_pool);
}

#endif  /* __RTNET_SOCKET_H_ */
