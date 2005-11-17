/*
 * Copyright (C) 2001-2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _XENO_SKIN_UVM_H
#define _XENO_SKIN_UVM_H

typedef unsigned long long nanotime_t;

typedef long long nanostime_t;

#ifndef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int uvm_system_info(xnsysinfo_t *infop);

int uvm_thread_shadow(const char *name,
		      void *uhandle,
		      void **khandlep);

int uvm_thread_create(const char *name,
		      void *uhandle,
		      xncompletion_t *completionp,
		      void **khandlep);

int uvm_thread_barrier(void);

int uvm_thread_start(void *khandle);

int uvm_thread_sync(xncompletion_t *completionp);

int uvm_thread_idle(unsigned long *lockp);

int uvm_thread_cancel(void *deadhandle,
		      void *nexthandle);

int uvm_thread_activate(void *nexthandle,
			void *prevhandle);

int uvm_thread_hold(unsigned long *pendp);

int uvm_thread_release(unsigned long *lockp);

int uvm_thread_set_periodic(nanotime_t idate,
			    nanotime_t period);

int uvm_thread_wait_period(void);

int uvm_timer_start(nanotime_t nstick);

int uvm_timer_stop(void);

int uvm_timer_read(nanotime_t *tp);

int uvm_timer_tsc(nanotime_t *tp);

int uvm_timer_ns2tsc(nanostime_t ns,
		     nanostime_t *ptsc);

int uvm_timer_tsc2ns(nanostime_t tsc,
		     nanostime_t *pns);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* !__KERNEL__ */

#endif /* !_XENO_SKIN_UVM_H */
