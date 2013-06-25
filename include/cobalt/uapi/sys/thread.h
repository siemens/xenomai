/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_UAPI_SYS_THREAD_H
#define _COBALT_UAPI_SYS_THREAD_H

#include <cobalt/uapi/sys/types.h>

/**
 * @ingroup nucleus
 * @defgroup nucleus_state_flags Thread state flags.
 * @brief Bits reporting permanent or transient states of thread.
 * @{
 */

/* State flags */

#define XNSUSP    0x00000001 /**< Suspended. */
#define XNPEND    0x00000002 /**< Sleep-wait for a resource. */
#define XNDELAY   0x00000004 /**< Delayed */
#define XNREADY   0x00000008 /**< Linked to the ready queue. */
#define XNDORMANT 0x00000010 /**< Not started yet or killed */
#define XNZOMBIE  0x00000020 /**< Zombie thread in deletion process */
#define XNSTARTED 0x00000080 /**< Thread has been started */
#define XNMAPPED  0x00000100 /**< Thread is mapped to a linux task */
#define XNRELAX   0x00000200 /**< Relaxed shadow thread (blocking bit) */
#define XNMIGRATE 0x00000400 /**< Thread is currently migrating to another CPU. */
#define XNHELD    0x00000800 /**< Thread is held to process emergency. */

#define XNBOOST   0x00001000 /**< Undergoes a PIP boost */
#define XNDEBUG   0x00002000 /**< Hit a debugger breakpoint */
#define XNLOCK    0x00004000 /**< Holds the scheduler lock (i.e. not preemptible) */
#define XNRRB     0x00008000 /**< Undergoes a round-robin scheduling */
#define XNTRAPSW  0x00010000 /**< Trap execution mode switches */
#define XNFPU     0x00020000 /**< Thread uses FPU */
#define XNROOT    0x00040000 /**< Root thread (that is, Linux/IDLE) */
#define XNWEAK    0x00080000 /**< Non real-time shadow (from the WEAK class) */
#define XNUSER    0x00100000 /**< Shadow thread running in userland */

/** @} */

/**
 * @ingroup nucleus
 * @defgroup nucleus_info_flags Thread information flags.
 * @brief Bits reporting events notified to the thread.
 * @{
 */

/* Information flags */

#define XNTIMEO   0x00000001 /**< Woken up due to a timeout condition */
#define XNRMID    0x00000002 /**< Pending on a removed resource */
#define XNBREAK   0x00000004 /**< Forcibly awaken from a wait state */
#define XNKICKED  0x00000008 /**< Forced out of primary mode */
#define XNWAKEN   0x00000010 /**< Thread waken up upon resource availability */
#define XNROBBED  0x00000020 /**< Robbed from resource ownership */
#define XNAFFSET  0x00000040 /**< CPU affinity changed from primary mode */
#define XNCANCELD 0x00000080 /**< Cancellation request is pending */
#define XNSWREP   0x00000100 /**< Mode switch already reported */

/** @} */

/*
 * Must follow strictly the declaration order of the state flags
 * defined above. Status symbols are defined as follows:
 *
 * 'S' -> Forcibly suspended.
 * 'w'/'W' -> Waiting for a resource, with or without timeout.
 * 'D' -> Delayed (without any other wait condition).
 * 'R' -> Runnable.
 * 'U' -> Unstarted or dormant.
 * 'X' -> Relaxed shadow.
 * 'H' -> Held in emergency.
 * 'b' -> Priority boost undergoing.
 * 'T' -> Ptraced and stopped.
 * 'l' -> Locks scheduler.
 * 'r' -> Undergoes round-robin.
 * 't' -> Mode switches trapped.
 */
#define XNTHREAD_STATE_LABELS  "SWDRU...X.HbTlrt...."

/**
 * @brief Structure containing thread information.
 */
struct xnthread_info {
	/**< Thread state, @see nucleus_state_flags */
	unsigned long state;
	/**< Base priority. */
	int bprio;
	/**< Current priority. May be subject to PI boost.*/
	int cprio;
	/**< CPU the thread currently runs on. */
	int cpu;
	/**< CPU affinity. */
	unsigned long affinity;
	/**< Time of next release.*/
	unsigned long long relpoint;
	/**< Execution time in primary mode (ns). */
	unsigned long long exectime;
	/**< Number of relaxes (i.e. secondary mode switches). */
	unsigned long modeswitches;
	/**< Number of context switches. */
	unsigned long ctxswitches;
	/**< Number of page faults. */
	unsigned long pagefaults;
	/**< Number of Xenomai syscalls. */
	unsigned long syscalls;
	/**< Symbolic name. */
	char name[XNOBJECT_NAME_LEN];
};

struct xnthread_user_window {
	unsigned long state;
	unsigned long grant_value;
};

#endif /* !_COBALT_UAPI_SYS_THREAD_H */
