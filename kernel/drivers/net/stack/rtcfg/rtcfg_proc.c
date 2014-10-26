/***
 *
 *  rtcfg/rtcfg_proc.c
 *
 *  Real-Time Configuration Distribution Protocol
 *
 *  Copyright (C) 2004 Jan Kiszka <jan.kiszka@web.de>
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

#include <rtdev.h>
#include <rtnet_internal.h>
#include <rtnet_port.h>
#include <rtcfg/rtcfg_conn_event.h>
#include <rtcfg/rtcfg_event.h>
#include <rtcfg/rtcfg_frame.h>


#ifdef CONFIG_PROC_FS
DEFINE_MUTEX(nrt_proc_lock);
static struct proc_dir_entry    *rtcfg_proc_root;



int rtcfg_proc_read_dev_state(char *buf, char **start, off_t offset, int count,
                              int *eof, void *data)
{
    struct rtcfg_device *rtcfg_dev = data;
    char *state_name[] = { "OFF", "SERVER_RUNNING", "CLIENT_0", "CLIENT_1",
                           "CLIENT_ANNOUNCED", "CLIENT_ALL_KNOWN",
                           "CLIENT_ALL_FRAMES", "CLIENT_2", "CLIENT_READY" };
    RTNET_PROC_PRINT_VARS(256);


    if (!RTNET_PROC_PRINT("state:\t\t\t%d (%s)\n"
                          "flags:\t\t\t%08X\n"
                          "other stations:\t\t%d\n"
                          "stations found:\t\t%d\n"
                          "stations ready:\t\t%d\n",
                          rtcfg_dev->state, state_name[rtcfg_dev->state],
                          rtcfg_dev->flags, rtcfg_dev->other_stations,
                          rtcfg_dev->stations_found,
                          rtcfg_dev->stations_ready))
        goto done;

    if (rtcfg_dev->state == RTCFG_MAIN_SERVER_RUNNING) {
        RTNET_PROC_PRINT("configured clients:\t%d\n"
                         "burstrate:\t\t%d\n"
                         "heartbeat period:\t%d ms\n",
                         rtcfg_dev->spec.srv.clients_configured,
                         rtcfg_dev->burstrate, rtcfg_dev->spec.srv.heartbeat);
    } else if (rtcfg_dev->state != RTCFG_MAIN_OFF) {
        RTNET_PROC_PRINT("address type:\t\t%d\n"
                         "server address:\t\t%02X:%02X:%02X:%02X:%02X:%02X\n"
                         "stage 2 config:\t\t%d/%d\n",
                         rtcfg_dev->spec.clt.addr_type,
                         rtcfg_dev->spec.clt.srv_mac_addr[0],
                         rtcfg_dev->spec.clt.srv_mac_addr[1],
                         rtcfg_dev->spec.clt.srv_mac_addr[2],
                         rtcfg_dev->spec.clt.srv_mac_addr[3],
                         rtcfg_dev->spec.clt.srv_mac_addr[4],
                         rtcfg_dev->spec.clt.srv_mac_addr[5],
                         rtcfg_dev->spec.clt.cfg_offs,
                         rtcfg_dev->spec.clt.cfg_len);
    }

  done:
    RTNET_PROC_PRINT_DONE;
}



int rtcfg_proc_read_stations(char *buf, char **start, off_t offset, int count,
                             int *eof, void *data)
{
    struct rtcfg_device     *rtcfg_dev = data;
    struct list_head        *entry;
    struct rtcfg_connection *conn;
    struct rtcfg_station    *station;
    int                     i;
    RTNET_PROC_PRINT_VARS_EX(80);


    if (mutex_lock_interruptible(&nrt_proc_lock))
        return -ERESTARTSYS;

    if (rtcfg_dev->state == RTCFG_MAIN_SERVER_RUNNING) {
        list_for_each(entry, &rtcfg_dev->spec.srv.conn_list) {
            conn = list_entry(entry, struct rtcfg_connection, entry);

            if ((conn->state != RTCFG_CONN_SEARCHING) &&
                (conn->state != RTCFG_CONN_DEAD) &&
                !RTNET_PROC_PRINT_EX("%02X:%02X:%02X:%02X:%02X:%02X\t%02X\n",
                                     conn->mac_addr[0], conn->mac_addr[1],
                                     conn->mac_addr[2], conn->mac_addr[3],
                                     conn->mac_addr[4], conn->mac_addr[5],
                                     conn->flags))
                break;
        }
    } else if (rtcfg_dev->spec.clt.station_addr_list) {
        for (i = 0; i < rtcfg_dev->stations_found; i++) {
            station = &rtcfg_dev->spec.clt.station_addr_list[i];

            if (!RTNET_PROC_PRINT_EX("%02X:%02X:%02X:%02X:%02X:%02X\t%02X\n",
                    station->mac_addr[0], station->mac_addr[1],
                    station->mac_addr[2], station->mac_addr[3],
                    station->mac_addr[4], station->mac_addr[5],
                    station->flags))
                break;
        }
    }

    mutex_unlock(&nrt_proc_lock);
    RTNET_PROC_PRINT_DONE_EX;
}



int rtcfg_proc_read_conn_state(char *buf, char **start, off_t offset,
                               int count, int *eof, void *data)
{
    struct rtcfg_connection *conn = data;
    char *state_name[] =
        { "SEARCHING", "STAGE_1", "STAGE_2", "READY", "DEAD" };
    RTNET_PROC_PRINT_VARS(512);


    if (!RTNET_PROC_PRINT("state:\t\t\t%d (%s)\n"
                          "flags:\t\t\t%02X\n"
                          "stage 1 size:\t\t%zd\n"
                          "stage 2 filename:\t%s\n"
                          "stage 2 size:\t\t%zd\n"
                          "stage 2 offset:\t\t%d\n"
                          "burstrate:\t\t%d\n"
                          "mac address:\t\t%02X:%02X:%02X:%02X:%02X:%02X\n",
                          conn->state, state_name[conn->state], conn->flags,
                          conn->stage1_size,
                          (conn->stage2_file)? conn->stage2_file->name: "-",
                          (conn->stage2_file)? conn->stage2_file->size: 0,
                          conn->cfg_offs, conn->burstrate,
                          conn->mac_addr[0], conn->mac_addr[1],
                          conn->mac_addr[2], conn->mac_addr[3],
                          conn->mac_addr[4], conn->mac_addr[5]))
        goto done;

#ifdef CONFIG_XENO_DRIVERS_NET_RTIPV4
    if ((conn->addr_type & RTCFG_ADDR_MASK) == RTCFG_ADDR_IP)
        RTNET_PROC_PRINT("ip:\t\t\t%u.%u.%u.%u\n",
                         NIPQUAD(conn->addr.ip_addr));
#endif /* CONFIG_XENO_DRIVERS_NET_RTIPV4 */

  done:
    RTNET_PROC_PRINT_DONE;
}



void rtcfg_update_conn_proc_entries(int ifindex)
{
    struct rtcfg_device     *dev = &device[ifindex];
    struct list_head        *entry;
    struct rtcfg_connection *conn;
    char                    name_buf[64];


    if (dev->state != RTCFG_MAIN_SERVER_RUNNING)
        return;

    list_for_each(entry, &dev->spec.srv.conn_list) {
        conn = list_entry(entry, struct rtcfg_connection, entry);

        switch (conn->addr_type & RTCFG_ADDR_MASK) {
#ifdef CONFIG_XENO_DRIVERS_NET_RTIPV4
            case RTCFG_ADDR_IP:
                snprintf(name_buf, 64, "CLIENT_%u.%u.%u.%u",
                         NIPQUAD(conn->addr.ip_addr));
                break;
#endif /* CONFIG_XENO_DRIVERS_NET_RTIPV4 */

            default: /* RTCFG_ADDR_MAC */
                snprintf(name_buf, 64,
                         "CLIENT_%02X%02X%02X%02X%02X%02X",
                         conn->mac_addr[0], conn->mac_addr[1],
                         conn->mac_addr[2], conn->mac_addr[3],
                         conn->mac_addr[4], conn->mac_addr[5]);
                break;
        }
        conn->proc_entry = create_proc_entry(name_buf,
            S_IFREG | S_IRUGO | S_IWUSR, dev->proc_entry);
        if (!conn->proc_entry)
            continue;

        conn->proc_entry->read_proc = rtcfg_proc_read_conn_state;
        conn->proc_entry->data      = conn;
    }
}



void rtcfg_remove_conn_proc_entries(int ifindex)
{
    struct rtcfg_device     *dev = &device[ifindex];
    struct list_head        *entry;
    struct rtcfg_connection *conn;


    if (dev->state != RTCFG_MAIN_SERVER_RUNNING)
        return;

    list_for_each(entry, &dev->spec.srv.conn_list) {
        conn = list_entry(entry, struct rtcfg_connection, entry);

        remove_proc_entry(conn->proc_entry->name, dev->proc_entry);
    }
}



void rtcfg_new_rtdev(struct rtnet_device *rtdev)
{
    struct rtcfg_device *dev = &device[rtdev->ifindex];
    struct proc_dir_entry   *proc_entry;


    mutex_lock(&nrt_proc_lock);

    dev->proc_entry = create_proc_entry(rtdev->name, S_IFDIR, rtcfg_proc_root);
    if (!dev->proc_entry)
        goto exit;

    proc_entry = create_proc_entry("state", S_IFREG | S_IRUGO | S_IWUSR,
                                   dev->proc_entry);
    if (!proc_entry)
        goto exit;
    proc_entry->read_proc = rtcfg_proc_read_dev_state;
    proc_entry->data      = dev;

    proc_entry = create_proc_entry("station_list", S_IFREG | S_IRUGO | S_IWUSR,
                                   dev->proc_entry);
    if (!proc_entry)
        goto exit;
    proc_entry->read_proc = rtcfg_proc_read_stations;
    proc_entry->data      = dev;

  exit:
    mutex_unlock(&nrt_proc_lock);
}



void rtcfg_remove_rtdev(struct rtnet_device *rtdev)
{
    struct rtcfg_device *dev = &device[rtdev->ifindex];


    // To-Do: issue down command

    mutex_lock(&nrt_proc_lock);

    if (dev->proc_entry) {
        rtcfg_remove_conn_proc_entries(rtdev->ifindex);

        remove_proc_entry("station_list", dev->proc_entry);
        remove_proc_entry("state", dev->proc_entry);
        remove_proc_entry(dev->proc_entry->name, rtcfg_proc_root);
        dev->proc_entry = NULL;
    }

    mutex_unlock(&nrt_proc_lock);
}



static struct rtdev_event_hook rtdev_hook = {
    .register_device =  rtcfg_new_rtdev,
    .unregister_device =rtcfg_remove_rtdev,
    .ifup =             NULL,
    .ifdown =           NULL
};



int rtcfg_init_proc(void)
{
    struct rtnet_device *rtdev;
    int                 i;


    rtcfg_proc_root = create_proc_entry("rtcfg", S_IFDIR, rtnet_proc_root);
    if (!rtcfg_proc_root)
        goto err1;

    for (i = 0; i < MAX_RT_DEVICES; i++) {
        rtdev = rtdev_get_by_index(i);
        if (rtdev) {
            rtcfg_new_rtdev(rtdev);
            rtdev_dereference(rtdev);
        }
    }

    rtdev_add_event_hook(&rtdev_hook);
    return 0;

  err1:
    /*ERRMSG*/printk("RTcfg: unable to initialise /proc entries\n");
    return -1;
}



void rtcfg_cleanup_proc(void)
{
    struct rtnet_device *rtdev;
    int                 i;


    rtdev_del_event_hook(&rtdev_hook);

    for (i = 0; i < MAX_RT_DEVICES; i++) {
        rtdev = rtdev_get_by_index(i);
        if (rtdev) {
            rtcfg_remove_rtdev(rtdev);
            rtdev_dereference(rtdev);
        }
    }

    remove_proc_entry("rtcfg", rtnet_proc_root);
}

#endif /* CONFIG_PROC_FS */
