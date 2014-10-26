/***
 *
 *  rtnet_internal.h - internal declarations
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

#ifndef __RTNET_INTERNAL_H_
#define __RTNET_INTERNAL_H_

#include <linux/module.h>

#include <rtnet_sys.h>


#ifdef CONFIG_XENO_DRIVERS_NET_CHECKED
#define RTNET_ASSERT(expr, func) \
    if (!(expr)) \
    { \
        rtdm_printk("Assertion failed! %s:%s:%d %s\n", \
        __FILE__, __FUNCTION__, __LINE__, (#expr)); \
        func \
    }
#else
#define RTNET_ASSERT(expr, func)
#endif /* CONFIG_XENO_DRIVERS_NET_CHECKED */

/* some configurables */

#define RTNET_DEF_STACK_PRIORITY \
    RTDM_TASK_HIGHEST_PRIORITY + RTDM_TASK_LOWER_PRIORITY
/*#define RTNET_RTDEV_PRIORITY        5*/


struct rtnet_device;

/*struct rtnet_msg {
    int                 msg_type;
    struct rtnet_device *rtdev;
};*/


struct rtnet_mgr {
    rtdm_task_t     task;
/*    MBX     mbx;*/
    rtdm_event_t    event;
};


extern struct rtnet_mgr STACK_manager;
extern struct rtnet_mgr RTDEV_manager;

extern const char rtnet_rtdm_provider_name[];


#ifdef CONFIG_PROC_FS

#include <linux/proc_fs.h>

extern struct proc_dir_entry *rtnet_proc_root;


/* Derived from Erwin Rol's rtai_proc_fs.h.
   Standard version assumes that output fits into the provided buffer,
   extended version also deals with potential fragmentation. */

#define RTNET_PROC_PRINT_VARS(MAX_BLOCK_LEN)                            \
    const int max_block_len = MAX_BLOCK_LEN;                            \
    off_t __limit           = count - MAX_BLOCK_LEN;                    \
    int   __len             = 0;                                        \
                                                                        \
    *eof = 1;                                                           \
    if (count < MAX_BLOCK_LEN)                                          \
        return 0

#define RTNET_PROC_PRINT(fmt, args...)                                  \
    ({                                                                  \
        __len += snprintf(buf + __len, max_block_len, fmt, ##args);     \
        (__len <= __limit);                                             \
    })

#define RTNET_PROC_PRINT_DONE                                           \
    return __len


#define RTNET_PROC_PRINT_VARS_EX(MAX_BLOCK_LEN)                         \
    const int max_block_len = MAX_BLOCK_LEN;                            \
    off_t __limit           = offset + count - MAX_BLOCK_LEN;           \
    off_t __pos             = 0;                                        \
    off_t __begin           = 0;                                        \
    int   __len             = 0;                                        \
                                                                        \
    *eof = 1;                                                           \
    if (count < MAX_BLOCK_LEN)                                          \
        return 0

#define RTNET_PROC_PRINT_EX(fmt, args...)                               \
    ({                                                                  \
        int len = snprintf(buf + __len, max_block_len, fmt, ##args);    \
        __len += len;                                                   \
        __pos += len;                                                   \
        if (__pos < offset) {                                           \
            __len = 0;                                                  \
            __begin = __pos;                                            \
        }                                                               \
        if (__pos > __limit)                                            \
            *eof = 0;                                                   \
        (__pos <= __limit);                                             \
    })

#define RTNET_PROC_PRINT_DONE_EX                                        \
    *start = buf + (offset - __begin);                                  \
    __len -= (offset - __begin);                                        \
    if (__len > count)                                                  \
        __len = count;                                                  \
    if (__len < 0)                                                      \
        __len = 0;                                                      \
    return __len;

#endif /* CONFIG_PROC_FS */


/* manage module reference counter */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

static inline void RTNET_MOD_INC_USE_COUNT_EX(struct module *module)
{
    if (!try_module_get(module))
        WARN_ON(1);
}

static inline void RTNET_MOD_DEC_USE_COUNT_EX(struct module *module)
{
    module_put(module);
}

#else

static inline void RTNET_MOD_INC_USE_COUNT_EX(struct module *module)
{
    __MOD_INC_USE_COUNT(module);
}

static inline void RTNET_MOD_DEC_USE_COUNT_EX(struct module *module)
{
    __MOD_DEC_USE_COUNT(module);
}

#endif

#define RTNET_MOD_INC_USE_COUNT RTNET_MOD_INC_USE_COUNT_EX(THIS_MODULE)
#define RTNET_MOD_DEC_USE_COUNT RTNET_MOD_DEC_USE_COUNT_EX(THIS_MODULE)

#define RTNET_SET_MODULE_OWNER(some_struct) \
    do { (some_struct)->rt_owner = THIS_MODULE; } while (0)


#ifndef list_for_each_entry
#define list_for_each_entry(pos, head, member)                      \
    for (pos = list_entry((head)->next, typeof(*pos), member),      \
                          prefetch(pos->member.next);               \
         &pos->member != (head);                                    \
         pos = list_entry(pos->member.next, typeof(*pos), member),  \
                          prefetch(pos->member.next))
#endif


#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
#include <linux/mutex.h>
#else
#include <asm/semaphore.h>
#define mutex                           semaphore
#define DEFINE_MUTEX(m)                 DECLARE_MUTEX(m)
#define mutex_init(m)                   init_MUTEX(m)
#define mutex_lock(m)                   down(m)
#define mutex_lock_interruptible(m)     down_interruptible(m)
#define mutex_unlock(m)                 up(m)
#endif

#endif /* __RTNET_INTERNAL_H_ */
