/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * I-pipe core -> legacy wrappers.
 */

#ifndef _XENO_ASM_GENERIC_IPIPE_WRAPPERS_H

#ifdef CONFIG_XENO_LEGACY_IPIPE
/*
 * CAUTION: These wrappers are scheduled for removal when a refactored
 * pipeline core implementation is available for each supported
 * architecture. We provide them only to be able to run over legacy
 * I-pipe patches until then.
 */
#include <linux/irq.h>
#include <ipipe/thread_info.h>

struct ipipe_trap_data {
	int exception;
	struct pt_regs *regs;
};

struct ipipe_work_header {
	size_t size;
	void (*handler)(struct ipipe_work_header *work);
};

#if !defined(CONFIG_XENO_OPT_HOSTRT) && !defined(CONFIG_HAVE_IPIPE_HOSTRT)
#define IPIPE_EVENT_HOSTRT  -1	/* Never received */
#endif
#define IPIPE_KEVT_SCHEDULE	IPIPE_EVENT_SCHEDULE
#define IPIPE_KEVT_SIGWAKE	IPIPE_EVENT_SIGWAKE
#define IPIPE_KEVT_EXIT		IPIPE_EVENT_EXIT
#define IPIPE_KEVT_CLEANUP	IPIPE_EVENT_CLEANUP
#define IPIPE_KEVT_HOSTRT	IPIPE_EVENT_HOSTRT
#define IPIPE_TRAP_MAYDAY	IPIPE_EVENT_RETURN

#define IPIPE_SYSCALL	1	/* Any non-zero value would do */
#define IPIPE_TRAP	2
#define IPIPE_KEVENT	4

#ifndef __IPIPE_FEATURE_PIC_MUTE
#define ipipe_mute_pic()		do { } while(0)
#define ipipe_unmute_pic()		do { } while(0)
#endif /* !__IPIPE_FEATURE_PIC_MUTE */

static inline void ipipe_register_head(struct ipipe_domain *ipd,
				       const char *name)
{
	struct ipipe_domain_attr attr;

	ipipe_init_attr(&attr);

	attr.name = "Xenomai";
	attr.entry = NULL;
	attr.domid = 0x58454e4f;
	attr.priority = IPIPE_HEAD_PRIORITY;
	ipipe_register_domain(&rthal_archdata.domain, &attr);
}

static inline void ipipe_unregister_head(struct ipipe_domain *ipd)
{
	ipipe_unregister_domain(&rthal_archdata.domain);
}

static inline int ipipe_request_irq(struct ipipe_domain *ipd,
				    unsigned int irq,
				    ipipe_irq_handler_t handler,
				    void *cookie,
				    ipipe_irq_ackfn_t ackfn)
{
	return ipipe_virtualize_irq(ipd, irq, handler, cookie, ackfn,
				    IPIPE_HANDLE_MASK | IPIPE_WIRED_MASK |
				    IPIPE_EXCLUSIVE_MASK);
}

static inline void ipipe_free_irq(struct ipipe_domain *ipd,
				  unsigned int irq)
{
	ipipe_virtualize_irq(ipd, irq, NULL, NULL, NULL, IPIPE_PASS_MASK);
}

static inline void ipipe_post_irq_root(unsigned int irq)
{
	__ipipe_schedule_irq_root(irq);
}

static inline void ipipe_post_irq_head(unsigned int irq)
{
	__ipipe_schedule_irq_head(irq);
}

static inline void ipipe_raise_irq(unsigned int irq)
{
	ipipe_trigger_irq(irq);
}

static inline void ipipe_stall_head(void)
{
	ipipe_stall_pipeline_head();
}

static inline unsigned long ipipe_test_and_stall_head(void)
{
	return ipipe_test_and_stall_pipeline_head();
}

static inline void ipipe_restore_head(unsigned long x)
{
	ipipe_restore_pipeline_head(x);
}

static inline void ipipe_unstall_head(void)
{
	ipipe_unstall_pipeline_head();
}

static inline unsigned long ipipe_test_head(void)
{
	return ipipe_test_pipeline_from(&rthal_archdata.domain);
}

#define ipipe_root_p ipipe_root_domain_p

static inline struct mm_struct *ipipe_get_active_mm(void)
{
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	return per_cpu(ipipe_active_mm, ipipe_processor_id());
#else
	return current->active_mm;
#endif
}

static inline int __ipipe_setscheduler_root(struct task_struct *p,
					  int policy,
					  int prio)
{
	return ipipe_setscheduler_root(p, policy, prio);
}

static inline int __ipipe_disable_ondemand_mappings(struct task_struct *p)
{
	return ipipe_disable_ondemand_mappings(p);
}

static inline void __ipipe_complete_domain_migration(void) { }

static inline void ipipe_raise_mayday(struct task_struct *p)
{
	ipipe_return_notify(p);
}

int xnarch_emulate_hooks(unsigned int event,
			 struct ipipe_domain *ipd,
			 void *data);

static inline void ipipe_set_hooks(struct ipipe_domain *ipd,
				   int enables)
{
	int (*fn)(unsigned int event, struct ipipe_domain *ipd, void *data);
	unsigned int ex;

	/*
	 * We don't care about the individual enable bits when
	 * emulating ipipe_set_hooks(). We are called once to
	 * enable/disable all events the nucleus needs to know about
	 * for a given domain.
	 */
	fn = enables ? xnarch_emulate_hooks : NULL;
	if (ipd == ipipe_root_domain) {
		ipipe_catch_event(ipd, IPIPE_EVENT_EXIT, fn);
		ipipe_catch_event(ipd, IPIPE_EVENT_SIGWAKE, fn);
		ipipe_catch_event(ipd, IPIPE_EVENT_SCHEDULE, fn);
		ipipe_catch_event(ipd, IPIPE_EVENT_CLEANUP, fn);
		ipipe_catch_event(ipd, IPIPE_EVENT_SYSCALL, fn);
#ifdef CONFIG_XENO_OPT_HOSTRT
		ipipe_catch_event(ipd, IPIPE_EVENT_HOSTRT, fn);
#endif
	} else {
		ipipe_catch_event(ipd, IPIPE_EVENT_RETURN, fn);
		ipipe_catch_event(ipd, IPIPE_EVENT_SYSCALL, fn);
		for (ex = 0; ex < IPIPE_NR_FAULTS; ex++)
			ipipe_catch_event(ipd, ex|IPIPE_EVENT_SELF, fn);
	}
}

void __ipipe_post_work_root(struct ipipe_work_header *work);

#define ipipe_post_work_root(p, header)			\
	do {						\
		void header_not_at_start(void);		\
		if (offsetof(typeof(*(p)), header)) {	\
			header_not_at_start();		\
		}					\
		__ipipe_post_work_root(&(p)->header);	\
	} while (0)

static inline
struct ipipe_threadinfo *ipipe_task_threadinfo(struct task_struct *p)
{
	static struct ipipe_threadinfo noinfo = {
		.thread = NULL,
		.mm = NULL,
	};
	return p->ptd[0] ?: &noinfo;
}

static inline
struct ipipe_threadinfo *ipipe_current_threadinfo(void)
{
	return ipipe_task_threadinfo(current);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37) && defined(CONFIG_GENERIC_HARDIRQS)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
#define irq_desc_get_chip(desc)	get_irq_desc_chip(desc)
#endif

/*
 * The irq chip descriptor has been heavily revamped in
 * 2.6.37. Provide generic accessors to the chip handlers we need for
 * kernels implementing those changes.
 */

static inline void ipipe_enable_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (WARN_ON_ONCE(chip->irq_unmask == NULL))
		return;

	chip->irq_unmask(&desc->irq_data);
}

static inline void ipipe_disable_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (WARN_ON_ONCE(chip->irq_mask == NULL))
		return;

	chip->irq_mask(&desc->irq_data);
}

#endif

static inline void ipipe_end_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	desc->ipipe_end(irq, desc);
}

static inline int hard_irqs_disabled(void)
{
	return irqs_disabled_hw();
}

static inline void hard_local_irq_disable(void)
{
	local_irq_disable_hw();
}

static inline void hard_local_irq_enable(void)
{
	local_irq_enable_hw();
}

static inline unsigned long hard_local_irq_save(void)
{
	unsigned long flags;

	local_irq_save_hw(flags);

	return flags;
}

static inline void hard_local_irq_restore(unsigned long flags)
{
	local_irq_restore_hw(flags);
}

static inline unsigned long hard_local_save_flags(void)
{
	unsigned long flags;

	local_save_flags_hw(flags);

	return flags;
}

static inline unsigned long hard_smp_local_irq_save(void)
{
	unsigned long flags;

	local_irq_save_hw_smp(flags);

	return flags;
}

static inline void hard_smp_local_irq_restore(unsigned long flags)
{
	local_irq_restore_hw_smp(flags);
}

static inline unsigned long hard_cond_local_irq_save(void)
{
	unsigned long flags;

	local_irq_save_hw_cond(flags);

	return flags;
}

static inline void hard_cond_local_irq_restore(unsigned long flags)
{
	local_irq_restore_hw_cond(flags);
}

#endif /* CONFIG_XENO_LEGACY_IPIPE */

#endif /* _XENO_ASM_GENERIC_IPIPE_WRAPPERS_H */
