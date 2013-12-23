/**
 * @file
 * @note Copyright (C) 2007,2012 Philippe Gerum <rpm@xenomai.org>.
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
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * @ingroup apc
 */

/**
 * @ingroup nucleus
 * @defgroup apc Asynchronous Procedure Calls
 *
 * APC is the acronym for Asynchronous Procedure Call, a mean by which
 * activities from the Xenomai domain can schedule deferred
 * invocations of handlers to be run into the Linux domain, as soon as
 * possible when the Linux kernel gets back in control. Up to
 * BITS_PER_LONG APC slots can be active at any point in time.
 *
 * APC support is built upon the interrupt pipeline's virtual
 * interrupt support.
 *
 *@{*/

#include <linux/spinlock.h>
#include <linux/ipipe.h>
#include <cobalt/kernel/apc.h>

static IPIPE_DEFINE_SPINLOCK(apc_lock);

void apc_dispatch(unsigned int virq, void *arg)
{
	void (*handler)(void *), *cookie;
	unsigned long *p;
	int apc;

	/*
	 * CAUTION: The APC dispatch loop is not protected against a
	 * handler becoming unavailable while processing the pending
	 * queue; the software must make sure to uninstall all APCs
	 * before eventually unloading any module that may contain APC
	 * handlers. We keep the handler affinity with the poster's
	 * CPU, so that the handler is invoked on the same CPU than
	 * the code which called xnapc_schedule().
	 */
	spin_lock(&apc_lock);

	/* This is atomic linux context (non-threaded IRQ). */
	p = &__this_cpu_ptr(&xnarch_percpu_machdata)->apc_pending;
	while (*p) {
		apc = ffnz(*p);
		clear_bit(apc, p);
		handler = xnarch_machdata.apc_table[apc].handler;
		cookie = xnarch_machdata.apc_table[apc].cookie;
		__this_cpu_ptr(&xnarch_percpu_machdata)->apc_shots[apc]++;
		spin_unlock(&apc_lock);
		handler(cookie);
		spin_lock(&apc_lock);
	}

	spin_unlock(&apc_lock);
}

/**
 * @fn int xnapc_alloc(const char *name,void (*handler)(void *cookie),void *cookie)
 *
 * @brief Allocate an APC slot.
 *
 * APC is the acronym for Asynchronous Procedure Call, a mean by which
 * activities from the Xenomai domain can schedule deferred
 * invocations of handlers to be run into the Linux domain, as soon as
 * possible when the Linux kernel gets back in control. Up to
 * BITS_PER_LONG APC slots can be active at any point in time. APC
 * support is built upon the interrupt pipeline's virtual interrupt
 * support.
 *
 * Any Linux kernel service which is callable from a regular Linux
 * interrupt handler is in essence available to APC handlers.
 *
 * @param name is a symbolic name identifying the APC which will get
 * reported through the /proc/xenomai/apc interface. Passing NULL to
 * create an anonymous APC is allowed.
 *
 * @param handler The address of the fault handler to call upon
 * exception condition. The handle will be passed the @a cookie value
 * unmodified.
 *
 * @param cookie A user-defined opaque pointer the APC handler
 * receives as its sole argument.
 *
 * @return a valid APC identifier is returned upon success, or a
 * negative error code otherwise:
 *
 * - -EINVAL is returned if @a handler is invalid.
 *
 * - -EBUSY is returned if no more APC slots are available.
 *
 * @remark Tags: none.
 */
int xnapc_alloc(const char *name,
		void (*handler)(void *cookie), void *cookie)
{
	unsigned long flags;
	int apc;

	if (handler == NULL)
		return -EINVAL;

	spin_lock_irqsave(&apc_lock, flags);

	if (xnarch_machdata.apc_map == ~0) {
		apc = -EBUSY;
		goto out;
	}

	apc = ffz(xnarch_machdata.apc_map);
	__set_bit(apc, &xnarch_machdata.apc_map);
	xnarch_machdata.apc_table[apc].handler = handler;
	xnarch_machdata.apc_table[apc].cookie = cookie;
	xnarch_machdata.apc_table[apc].name = name;
out:
	spin_unlock_irqrestore(&apc_lock, flags);

	return apc;
}
EXPORT_SYMBOL_GPL(xnapc_alloc);

/**
 * @fn int xnapc_free(int apc)
 *
 * @brief Releases an APC slot.
 *
 * This service deallocates an APC slot obtained by xnapc_alloc().
 *
 * @param apc The APC id. to release, as returned by a successful call
 * to the xnapc_alloc() service.
 *
 * @remark Tags: none.
 */
void xnapc_free(int apc)
{
	BUG_ON(apc < 0 || apc >= BITS_PER_LONG);
	clear_bit(apc, &xnarch_machdata.apc_map);
	smp_mb__after_clear_bit();
}
EXPORT_SYMBOL_GPL(xnapc_free);

/*@}*/
