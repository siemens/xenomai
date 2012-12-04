/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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
 */

#ifndef _XENO_ASM_NIOS2_THREAD_H
#define _XENO_ASM_NIOS2_THREAD_H

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/xenomai/wrappers.h>

#ifndef CONFIG_MMU
#error "Xenomai: please use Xenomai 2.5.x for MMU-less support"
#endif

struct xnthread;
struct task_struct;

struct xnarchtcb {
	unsigned int stacksize;
	unsigned long *stackbase;
	struct task_struct *user_task;
	struct task_struct *active_task;
	struct thread_struct *tsp;
	struct mm_struct *mm;
	struct mm_struct *active_mm;
	struct thread_struct ts;
	struct {
		unsigned long ea;
		unsigned long r2;
		unsigned long r3;
	} mayday;
	struct xnthread *self;
	int imask;
	const char *name;
	void (*entry)(void *cookie);
	void *cookie;
};

#define XNARCH_THREAD_STACKSZ   4096

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_stack_base(tcb)	((tcb)->stackbase)
#define xnarch_stack_end(tcb)	((caddr_t)(tcb)->stackbase - (tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)
#define xnarch_fpu_ptr(tcb)     NULL
#define xnarch_fault_trap(d)   ((d)->exception)
#define xnarch_fault_code(d)   (0) /* None on this arch. */
#define xnarch_fault_pc(d)     ((d)->regs->ea)
#define xnarch_fault_fpu_p(d)  (0) /* Can't be. */
#define xnarch_fault_pf_p(d)   (0) /* No page faults. */
#define xnarch_fault_bp_p(d)   ((current->ptrace & PT_PTRACED) &&	\
				((d)->exception == IPIPE_TRAP_BP))

#define xnarch_fault_notify(d) (xnarch_fault_bp_p(d) == 0)

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb);

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *),
			void *cookie,
			int imask,
			struct xnthread *thread, char *name);

void xnarch_leave_root(struct xnarchtcb *rootcb);

int xnarch_escalate(void);

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread,
			  const char *name);

void xnarch_init_shadow_tcb(struct xnarchtcb *tcb,
			    struct xnthread *thread,
			    const char *name);

void xnarch_init_tcb(struct xnarchtcb *tcb);

int xnarch_alloc_stack(struct xnarchtcb *tcb, size_t stacksize);

void xnarch_free_stack(struct xnarchtcb *tcb);

static inline void xnarch_enter_root(struct xnarchtcb *rootcb) { }
static inline void xnarch_enable_fpu(struct xnarchtcb *current_tcb) { }
static inline void xnarch_init_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_save_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_restore_fpu(struct xnarchtcb *tcb) { }

static inline int xnarch_fpu_init_p(struct task_struct *task)
{
	return 0;
}

#endif /* !_XENO_ASM_NIOS2_THREAD_H */
