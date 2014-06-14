/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_KERNEL_INTR_H
#define _COBALT_KERNEL_INTR_H

#include <cobalt/kernel/stat.h>

/**
 * @addtogroup cobalt_core_irq
 * @{
 */

/* Possible return values of ISR. */
#define XN_ISR_NONE	 0x1
#define XN_ISR_HANDLED	 0x2
/* Additional bits. */
#define XN_ISR_PROPAGATE 0x100
#define XN_ISR_NOENABLE  0x200
#define XN_ISR_BITMASK	 (~0xff)

/* Creation flags. */
#define XN_ISR_SHARED	 0x1
#define XN_ISR_EDGE	 0x2

/* Operational flags. */
#define XN_ISR_ATTACHED	 0x10000

struct xnintr;
struct xnsched;

typedef int (*xnisr_t)(struct xnintr *intr);

typedef void (*xniack_t)(unsigned irq, void *arg);

struct xnirqstat {
	/* !< Number of handled receipts since attachment. */
	xnstat_counter_t hits;
	/* !< Runtime accounting entity */
	xnstat_exectime_t account;
	/* !< Accumulated accounting entity */
	xnstat_exectime_t sum;
};

struct xnintr {
#ifdef CONFIG_XENO_OPT_SHIRQ
	/* !< Next object in the IRQ-sharing chain. */
	struct xnintr *next;
#endif /* CONFIG_XENO_OPT_SHIRQ */
	/* !< Number of consequent unhandled interrupts */
	unsigned int unhandled;
	/* !< Interrupt service routine. */
	xnisr_t isr;
	/* !< User-defined cookie value. */
	void *cookie;
	/* !< Creation flags. */
	int flags;
	/* !< IRQ number. */
	unsigned int irq;
	/* !< Interrupt acknowledge routine. */
	xniack_t iack;
	/* !< Symbolic name. */
	const char *name;
	/* !< Statistics. */
	struct xnirqstat *stats;
};

struct xnintr_iterator {
    int cpu;		/* !< Current CPU in iteration. */
    unsigned long hits;	/* !< Current hit counter. */
    xnticks_t exectime_period;	/* !< Used CPU time in current accounting period. */
    xnticks_t account_period; /* !< Length of accounting period. */
    xnticks_t exectime_total;	/* !< Overall CPU time consumed. */
    int list_rev;	/* !< System-wide xnintr list revision (internal use). */
    struct xnintr *prev;	/* !< Previously visited xnintr object (internal use). */
};

extern struct xnintr nktimer;

int xnintr_mount(void);

void xnintr_core_clock_handler(void);

void xnintr_host_tick(struct xnsched *sched);

void xnintr_init_proc(void);

void xnintr_cleanup_proc(void);

    /* Public interface. */

int xnintr_init(struct xnintr *intr,
		const char *name,
		unsigned irq,
		xnisr_t isr,
		xniack_t iack,
		int flags);

void xnintr_destroy(struct xnintr *intr);

int xnintr_attach(struct xnintr *intr,
		  void *cookie);

void xnintr_detach(struct xnintr *intr);

void xnintr_enable(struct xnintr *intr);

void xnintr_disable(struct xnintr *intr);

void xnintr_affinity(struct xnintr *intr,
		     cpumask_t cpumask);

int xnintr_query_init(struct xnintr_iterator *iterator);

int xnintr_get_query_lock(void);

void xnintr_put_query_lock(void);

int xnintr_query_next(int irq, struct xnintr_iterator *iterator,
		      char *name_buf);

/** @} */

#endif /* !_COBALT_KERNEL_INTR_H */
