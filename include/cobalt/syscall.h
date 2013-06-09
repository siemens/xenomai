/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COBALT_SYSCALL_H
#define _COBALT_SYSCALL_H

#include <asm/xenomai/syscall.h>

#define COBALT_BINDING_MAGIC            0x50534531

#define sc_cobalt_thread_create         0
/* 1-2 unimplemented */
#define sc_cobalt_sched_yield           3
#define sc_cobalt_thread_make_periodic  4
#define sc_cobalt_thread_wait           5
#define sc_cobalt_thread_set_mode       6
#define sc_cobalt_thread_set_name       7
#define sc_cobalt_sem_init              8
#define sc_cobalt_sem_destroy           9
#define sc_cobalt_sem_post              10
#define sc_cobalt_sem_wait              11
#define sc_cobalt_sem_trywait           12
#define sc_cobalt_sem_getvalue          13
#define sc_cobalt_clock_getres          14
#define sc_cobalt_clock_gettime         15
#define sc_cobalt_clock_settime         16
#define sc_cobalt_clock_nanosleep       17
#define sc_cobalt_mutex_init            18
#define sc_cobalt_check_init            19
#define sc_cobalt_mutex_destroy         20
#define sc_cobalt_mutex_lock            21
#define sc_cobalt_mutex_timedlock       22
#define sc_cobalt_mutex_trylock         23
#define sc_cobalt_mutex_unlock          24
#define sc_cobalt_cond_init             25
#define sc_cobalt_cond_destroy          26
#define sc_cobalt_cond_wait_prologue    27
#define sc_cobalt_cond_wait_epilogue    28
/* 30-31 unimplemented */
#define sc_cobalt_mq_open               31
#define sc_cobalt_mq_close              32
#define sc_cobalt_mq_unlink             33
#define sc_cobalt_mq_getattr            34
#define sc_cobalt_mq_setattr            35
#define sc_cobalt_mq_send               36
#define sc_cobalt_mq_timedsend          37
#define sc_cobalt_mq_receive            38
#define sc_cobalt_mq_timedreceive       39
#define sc_cobalt_thread_probe          40
#define sc_cobalt_sched_minprio         41
#define sc_cobalt_sched_maxprio         42
#define sc_cobalt_timer_create          43
#define sc_cobalt_timer_delete          44
#define sc_cobalt_timer_settime         45
#define sc_cobalt_timer_gettime         46
#define sc_cobalt_timer_getoverrun      47
#define sc_cobalt_sem_open              48
#define sc_cobalt_sem_close             49
#define sc_cobalt_sem_unlink            50
#define sc_cobalt_sem_timedwait         51
#define sc_cobalt_mq_notify             52
/* 53-60 unimplemented */
#define sc_cobalt_mutexattr_init        61
#define sc_cobalt_mutexattr_destroy     62
#define sc_cobalt_mutexattr_gettype     63
#define sc_cobalt_mutexattr_settype     64
#define sc_cobalt_mutexattr_getprotocol 65
#define sc_cobalt_mutexattr_setprotocol 66
#define sc_cobalt_mutexattr_getpshared  67
#define sc_cobalt_mutexattr_setpshared  68
#define sc_cobalt_condattr_init         69
#define sc_cobalt_condattr_destroy      70
#define sc_cobalt_condattr_getclock     71
#define sc_cobalt_condattr_setclock     72
#define sc_cobalt_condattr_getpshared   73
#define sc_cobalt_condattr_setpshared   74
/* 75 unimplemented */
#define sc_cobalt_thread_kill           76
#define sc_cobalt_select                77
#define sc_cobalt_thread_setschedparam_ex	78
#define sc_cobalt_thread_getschedparam_ex	79
#define sc_cobalt_sem_init_np           80
#define sc_cobalt_sem_broadcast_np      81
#define sc_cobalt_thread_getstat        82
#define sc_cobalt_monitor_init          83
#define sc_cobalt_monitor_destroy       84
#define sc_cobalt_monitor_enter         85
#define sc_cobalt_monitor_wait          86
#define sc_cobalt_monitor_sync          87
#define sc_cobalt_monitor_exit          88
#define sc_cobalt_event_init            89
#define sc_cobalt_event_wait            90
#define sc_cobalt_event_sync            91
#define sc_cobalt_event_destroy         92
#define sc_cobalt_sched_setconfig_np	93

#ifdef __KERNEL__

int cobalt_syscall_init(void);

void cobalt_syscall_cleanup(void);

#endif /* __KERNEL__ */

#endif /* _COBALT_SYSCALL_H */
