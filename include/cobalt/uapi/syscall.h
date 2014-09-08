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

#define sc_cobalt_bind				0
#define sc_cobalt_thread_create         	1
#define sc_cobalt_thread_getpid         	2
#define sc_cobalt_thread_setmode        	3
#define sc_cobalt_thread_setname        	4
#define sc_cobalt_thread_join           	5
#define sc_cobalt_thread_kill           	6
#define sc_cobalt_thread_setschedparam_ex	7
#define sc_cobalt_thread_getschedparam_ex	8
#define sc_cobalt_thread_getstat        	9
/* 10 unimp */
#define sc_cobalt_sem_init              	11
#define sc_cobalt_sem_destroy           	12
#define sc_cobalt_sem_post              	13
#define sc_cobalt_sem_wait              	14
#define sc_cobalt_sem_trywait           	15
#define sc_cobalt_sem_getvalue          	16
#define sc_cobalt_sem_open              	17
#define sc_cobalt_sem_close             	18
#define sc_cobalt_sem_unlink            	19
#define sc_cobalt_sem_timedwait         	20
#define sc_cobalt_sem_inquire           	21
/* 22 unimp */
#define sc_cobalt_sem_broadcast_np      	23
#define sc_cobalt_clock_getres          	24
#define sc_cobalt_clock_gettime         	25
#define sc_cobalt_clock_settime         	26
#define sc_cobalt_clock_nanosleep       	27
#define sc_cobalt_mutex_init            	28
#define sc_cobalt_mutex_check_init      	29
#define sc_cobalt_mutex_destroy         	30
#define sc_cobalt_mutex_lock            	31
#define sc_cobalt_mutex_timedlock       	32
#define sc_cobalt_mutex_trylock         	33
#define sc_cobalt_mutex_unlock          	34
#define sc_cobalt_cond_init             	35
#define sc_cobalt_cond_destroy          	36
#define sc_cobalt_cond_wait_prologue    	37
#define sc_cobalt_cond_wait_epilogue    	38
#define sc_cobalt_mq_open               	39
#define sc_cobalt_mq_close              	40
#define sc_cobalt_mq_unlink             	41
#define sc_cobalt_mq_getattr            	42
#define sc_cobalt_mq_setattr            	43
#define sc_cobalt_mq_timedsend          	44
#define sc_cobalt_mq_timedreceive       	45
#define sc_cobalt_mq_notify             	46
#define sc_cobalt_sched_minprio         	47
#define sc_cobalt_sched_maxprio         	48
#define sc_cobalt_sched_weightprio      	49
#define sc_cobalt_sched_yield           	50
#define sc_cobalt_sched_setconfig_np		51
#define sc_cobalt_sched_getconfig_np		52
#define sc_cobalt_timer_create          	53
#define sc_cobalt_timer_delete          	54
#define sc_cobalt_timer_settime         	55
#define sc_cobalt_timer_gettime         	56
#define sc_cobalt_timer_getoverrun      	57
#define sc_cobalt_timerfd_create		58
#define sc_cobalt_timerfd_settime		59
#define sc_cobalt_timerfd_gettime		60
#define sc_cobalt_sigwait			61
#define sc_cobalt_sigwaitinfo			62
#define sc_cobalt_sigtimedwait			63
#define sc_cobalt_sigpending			64
#define sc_cobalt_kill				65
#define sc_cobalt_sigqueue			66
#define sc_cobalt_monitor_init          	67
#define sc_cobalt_monitor_destroy       	68
#define sc_cobalt_monitor_enter         	69
#define sc_cobalt_monitor_wait          	70
#define sc_cobalt_monitor_sync          	71
#define sc_cobalt_monitor_exit          	72
#define sc_cobalt_event_init            	73
#define sc_cobalt_event_wait            	74
#define sc_cobalt_event_sync            	75
#define sc_cobalt_event_destroy         	76
#define sc_cobalt_event_inquire         	77
#define sc_cobalt_open				78
#define sc_cobalt_socket			79
#define sc_cobalt_close				80
#define sc_cobalt_ioctl				81
#define sc_cobalt_read				82
#define sc_cobalt_write				83
#define sc_cobalt_recvmsg         		84
#define sc_cobalt_sendmsg         		85
#define sc_cobalt_mmap            		86
#define sc_cobalt_select                	87
#define sc_cobalt_migrate			88
#define sc_cobalt_archcall			89
#define sc_cobalt_info				90
#define sc_cobalt_trace				91
#define sc_cobalt_sysctl			92
#define sc_cobalt_get_current			93
#define sc_cobalt_mayday			94
#define sc_cobalt_backtrace			95
#define sc_cobalt_serialdbg			96
#define sc_cobalt_extend			97
#define sc_cobalt_sysconf			98

#define __NR_COBALT_SYSCALLS			100

#endif /* !_COBALT_UAPI_SYSCALL_H */
