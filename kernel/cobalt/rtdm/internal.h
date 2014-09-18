/*
 * Copyright (C) 2005-2007 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>.
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

#ifndef _RTDM_INTERNAL_H
#define _RTDM_INTERNAL_H

#include <linux/list.h>
#include <linux/sem.h>
#include <linux/mutex.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/tree.h>
#include <rtdm/driver.h>

#define RTDM_FD_MAX			CONFIG_XENO_OPT_RTDM_FILDES

#define DEF_DEVNAME_HASHTAB_SIZE	256	/* entries in name hash table */
#define DEF_PROTO_HASHTAB_SIZE		256	/* entries in protocol hash table */

struct rtdm_fd;

DECLARE_EXTERN_XNLOCK(rt_fildes_lock);
DECLARE_EXTERN_XNLOCK(rt_dev_lock);

extern int open_fildes;
extern struct semaphore nrt_dev_lock;
extern struct list_head rtdm_named_devices;
extern struct rb_root rtdm_protocol_devices;

static inline void rtdm_dereference_device(struct rtdm_device *device)
{
	atomic_dec(&device->refcount);
}

int __init rtdm_dev_init(void);
void rtdm_dev_cleanup(void);

#ifdef CONFIG_XENO_OPT_VFILE
int rtdm_proc_init(void);
void rtdm_proc_cleanup(void);
#else
static inline int rtdm_proc_init(void) { return 0; }
static void inline rtdm_proc_cleanup(void) { }
#endif

void __rt_dev_close(struct rtdm_fd *fd);

int __rt_dev_ioctl_fallback(struct rtdm_fd *fd,
			    unsigned int request, void __user *arg);

void __rt_dev_unref(struct rtdm_fd *fd, unsigned int idx);

int __rtdm_mmap_from_fdop(struct rtdm_fd *fd, size_t len, off_t offset,
			  int prot, int flags, void *__user *pptr);

struct rtdm_device *__rtdm_get_namedev(const char *path);

struct rtdm_device *__rtdm_get_protodev(int protocol_family, int socket_type);

int rtdm_init(void);

void rtdm_cleanup(void);

#endif /* _RTDM_INTERNAL_H */
