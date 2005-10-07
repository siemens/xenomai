/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
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

#ifndef _RTDM_CORE_H
#define _RTDM_CORE_H

#include <nucleus/pod.h>
#include <rtdm/rtdm_driver.h>


#define DEF_FILDES_COUNT    64  /* default number of file descriptors */


struct rtdm_fildes {
    struct rtdm_fildes                  *next;
    volatile struct rtdm_dev_context    *context;
};


#ifdef CONFIG_SMP
extern xnlock_t             rt_fildes_lock;
#endif /* CONFIG_SMP */

extern unsigned int         fd_count;
extern struct rtdm_fildes   *fildes_table;
extern int                  open_fildes;


int __init rtdm_core_init(void);

static inline void rtdm_core_cleanup(void)
{
    kfree(fildes_table);
}

#endif /* _RTDM_CORE_H */
