/*
 * Copyright (C) 2006 Wolfgang Grandegger <wg@grandegger.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __RTCAN_RAW_H_
#define __RTCAN_RAW_H_

#ifdef __KERNEL__

int rtcan_raw_ioctl_dev(struct rtdm_dev_context *context,
			rtdm_user_info_t *user_info, int request, void *arg);

int rtcan_raw_check_filter(struct rtcan_socket *sock,
			   int ifindex, struct rtcan_filter_list *flist);
int rtcan_raw_add_filter(struct rtcan_socket *sock, int ifindex);
void rtcan_raw_remove_filter(struct rtcan_socket *sock);

void rtcan_rcv(struct rtcan_device *rtcandev, struct rtcan_skb *skb);

void rtcan_tx_loopback(struct rtcan_device *rtcandev);
#ifdef CONFIG_XENO_DRIVERS_CAN_TX_LOOPBACK
#define rtcan_tx_loopback_enabled(sock) (sock->tx_loopback)
#define rtcan_tx_loopback_pending(dev) (dev->tx_socket)
#else /* !CONFIG_XENO_DRIVERS_CAN_TX_LOOPBACK */
#define rtcan_tx_loopback_enabled(sock) (0)
#define rtcan_tx_loopback_pending(dev) (0)
#endif /* CONFIG_XENO_DRIVERS_CAN_TX_LOOPBACK */

int __init rtcan_raw_proto_register(void);
void __exit rtcan_raw_proto_unregister(void);

#endif  /* __KERNEL__ */

#endif  /* __RTCAN_RAW_H_ */
