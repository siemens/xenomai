/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_SYSCALL_H
#define _COBALT_UAPI_SYSCALL_H

#include <cobalt/uapi/asm-generic/syscall.h>

#define COBALT_BINDING_MAGIC            0x50534531

#define sc_cobalt_thread_create         0
#define sc_cobalt_thread_getpid         1
#define sc_cobalt_sched_weightprio      2
#define sc_cobalt_sched_yield           3
/* 4-5 unimplemented */
#define sc_cobalt_thread_setmode        6
#define sc_cobalt_thread_setname        7
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
#define sc_cobalt_mutex_check_init      19
#define sc_cobalt_mutex_destroy         20
#define sc_cobalt_mutex_lock            21
#define sc_cobalt_mutex_timedlock       22
#define sc_cobalt_mutex_trylock         23
#define sc_cobalt_mutex_unlock          24
#define sc_cobalt_cond_init             25
#define sc_cobalt_cond_destroy          26
#define sc_cobalt_cond_wait_prologue    27
#define sc_cobalt_cond_wait_epilogue    28
/* 29-30 unimplemented */
#define sc_cobalt_mq_open               31
#define sc_cobalt_mq_close              32
#define sc_cobalt_mq_unlink             33
#define sc_cobalt_mq_getattr            34
#define sc_cobalt_mq_setattr            35
/* 36 unimplemented */
#define sc_cobalt_mq_timedsend          37
/* 38 unimplemented */
#define sc_cobalt_mq_timedreceive       39
/* 40 unimplemented */
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
#define sc_cobalt_sigwait		53
#define sc_cobalt_sigwaitinfo		54
#define sc_cobalt_sigtimedwait		55
#define sc_cobalt_sigpending		56
#define sc_cobalt_kill			57
#define sc_cobalt_sem_inquire           58
#define sc_cobalt_event_inquire         59
#define sc_cobalt_sigqueue		60
/* 61-74 unimplemented */
#define sc_cobalt_thread_join           75
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
#define sc_cobalt_sched_getconfig_np	94
#define sc_cobalt_timerfd_create	95
#define sc_cobalt_timerfd_settime	96
#define sc_cobalt_timerfd_gettime	97

#define __NR_COBALT_SYSCALLS		98

#endif /* !_COBALT_UAPI_SYSCALL_H */
