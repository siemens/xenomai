/***
 *
 *  rtmac_proc.c
 *
 *  rtmac - real-time networking medium access control subsystem
 *  Copyright (C) 2002 Marc Kleine-Budde <kleine-budde@gmx.de>
 *                2004 Jan Kiszka <jan.kiszka@web.de>
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

#include <rtnet_internal.h>
#include <rtmac/rtmac_disc.h>
#include <rtmac/rtmac_vnic.h>
#include <rtmac/rtmac_proc.h>


#ifdef CONFIG_PROC_FS
struct proc_dir_entry *rtmac_proc_root;


int rtmac_disc_proc_register(struct rtmac_disc *disc)
{
    int                     i;
    struct proc_dir_entry   *proc_entry;


    i = 0;
    while (disc->proc_entries[i].name != NULL) {
        proc_entry = create_proc_entry(disc->proc_entries[i].name,
                                       S_IFREG | S_IRUGO | S_IWUSR,
                                       rtmac_proc_root);
        if (!proc_entry) {
            while (--i > 0) {
                remove_proc_entry(disc->proc_entries[i].name, rtmac_proc_root);
                i--;
            }
            return -1;
        }

        proc_entry->read_proc = disc->proc_entries[i].handler;
        i++;
    }

    return 0;
}



void rtmac_disc_proc_unregister(struct rtmac_disc *disc)
{
    int i;


    i = 0;
    while (disc->proc_entries[i].name != NULL) {
        remove_proc_entry(disc->proc_entries[i].name, rtmac_proc_root);
        i++;
    }
}



int rtmac_proc_register(void)
{
    struct proc_dir_entry *proc_entry;


    rtmac_proc_root = create_proc_entry("rtmac", S_IFDIR, rtnet_proc_root);
    if (!rtmac_proc_root)
        goto err1;

    proc_entry = create_proc_entry("disciplines", S_IFREG | S_IRUGO | S_IWUSR,
                                   rtmac_proc_root);
    if (!proc_entry)
        goto err2;
    proc_entry->read_proc = rtmac_proc_read_disc;

    proc_entry = create_proc_entry("vnics", S_IFREG | S_IRUGO | S_IWUSR,
                                   rtmac_proc_root);
    if (!proc_entry)
        goto err3;
    proc_entry->read_proc = rtmac_proc_read_vnic;

    return 0;

  err3:
    remove_proc_entry("disciplines", rtmac_proc_root);

  err2:
    remove_proc_entry("rtmac", rtnet_proc_root);

  err1:
    /*ERRMSG*/printk("RTmac: unable to initialize /proc entries\n");
    return -1;
}



void rtmac_proc_release(void)
{
    remove_proc_entry("vnics", rtmac_proc_root);
    remove_proc_entry("disciplines", rtmac_proc_root);
    remove_proc_entry("rtmac", rtnet_proc_root);
}

#endif /* CONFIG_PROC_FS */
