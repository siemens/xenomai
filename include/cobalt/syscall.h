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

#ifndef _POSIX_SYSCALL_H
#define _POSIX_SYSCALL_H

#ifndef __XENO_SIM__
#include <asm/xenomai/syscall.h>
#endif /* __XENO_SIM__ */

#define __cobalt_thread_create         0
#define __cobalt_thread_detach         1
#define __cobalt_thread_setschedparam  2
#define __cobalt_sched_yield           3
#define __cobalt_thread_make_periodic  4
#define __cobalt_thread_wait           5
#define __cobalt_thread_set_mode       6
#define __cobalt_thread_set_name       7
#define __cobalt_sem_init              8
#define __cobalt_sem_destroy           9
#define __cobalt_sem_post              10
#define __cobalt_sem_wait              11
#define __cobalt_sem_trywait           12
#define __cobalt_sem_getvalue          13
#define __cobalt_clock_getres          14
#define __cobalt_clock_gettime         15
#define __cobalt_clock_settime         16
#define __cobalt_clock_nanosleep       17
#define __cobalt_mutex_init            18
#define __cobalt_check_init            19
#define __cobalt_mutex_destroy         20
#define __cobalt_mutex_lock            21
#define __cobalt_mutex_timedlock       22
#define __cobalt_mutex_trylock         23
#define __cobalt_mutex_unlock          24
#define __cobalt_cond_init             25
#define __cobalt_cond_destroy          26
#define __cobalt_cond_wait_prologue    27
#define __cobalt_cond_wait_epilogue    28
#define __cobalt_cond_signal           29
#define __cobalt_cond_broadcast        30
#define __cobalt_mq_open               31
#define __cobalt_mq_close              32
#define __cobalt_mq_unlink             33
#define __cobalt_mq_getattr            34
#define __cobalt_mq_setattr            35
#define __cobalt_mq_send               36
#define __cobalt_mq_timedsend          37
#define __cobalt_mq_receive            38
#define __cobalt_mq_timedreceive       39
#define __cobalt_thread_probe          40
#define __cobalt_sched_minprio         41
#define __cobalt_sched_maxprio         42
#define __cobalt_timer_create          43
#define __cobalt_timer_delete          44
#define __cobalt_timer_settime         45
#define __cobalt_timer_gettime         46
#define __cobalt_timer_getoverrun      47
#define __cobalt_sem_open              48
#define __cobalt_sem_close             49
#define __cobalt_sem_unlink            50
#define __cobalt_sem_timedwait         51
#define __cobalt_mq_notify             52
#define __cobalt_shm_open              53
#define __cobalt_shm_unlink            54
#define __cobalt_shm_close             55
#define __cobalt_ftruncate             56
#define __cobalt_mmap_prologue         57
#define __cobalt_mmap_epilogue         58
#define __cobalt_munmap_prologue       59
#define __cobalt_munmap_epilogue       60
#define __cobalt_mutexattr_init        61
#define __cobalt_mutexattr_destroy     62
#define __cobalt_mutexattr_gettype     63
#define __cobalt_mutexattr_settype     64
#define __cobalt_mutexattr_getprotocol 65
#define __cobalt_mutexattr_setprotocol 66
#define __cobalt_mutexattr_getpshared  67
#define __cobalt_mutexattr_setpshared  68
#define __cobalt_condattr_init         69
#define __cobalt_condattr_destroy      70
#define __cobalt_condattr_getclock     71
#define __cobalt_condattr_setclock     72
#define __cobalt_condattr_getpshared   73
#define __cobalt_condattr_setpshared   74
#define __cobalt_thread_getschedparam  75
#define __cobalt_thread_kill           76
#define __cobalt_select                77
#define __cobalt_thread_setschedparam_ex	78
#define __cobalt_thread_getschedparam_ex	79
#define __cobalt_sem_init_np           80
#define __cobalt_sem_broadcast_np      81
#define __cobalt_thread_getstat        82

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int cobalt_syscall_init(void);

void cobalt_syscall_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _POSIX_SYSCALL_H */
