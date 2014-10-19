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
#include <linux/types.h>
#include <linux/ipipe.h>
#include <linux/vmalloc.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/uapi/syscall.h>
#include <asm/cacheflush.h>
#include <asm/ptrace.h>

static void *mayday;

static inline void setup_mayday(void *page)
{
	/*
	 * We want this code to appear at the top of the MAYDAY page:
	 *
	 *	00c00334	movhi	r3,#sc_cobalt_mayday
	 *	18c08ac4	addi	r3,r3,#cobalt_syscall_tag
	 *	00800004	movi	r2,0
	 *	003b683a	trap
	 *	003fff06	br	.
	 */
	static const struct {
		u32 movhi_r3h;
		u32 addi_r3l;
		u32 movi_r2;
		u32 syscall;
		u32 bug;
	} code = {
		.movhi_r3h = 0x00c00334,
		.addi_r3l = 0x18c08ac4,
		.movi_r2 = 0x00800004,
		.syscall = 0x003b683a,
		.bug = 0x003fff06
	};

	memcpy(page, &code, sizeof(code));

	flush_dcache_range((unsigned long)page,
			   (unsigned long)page + sizeof(code));
}

int xnarch_init_mayday(void)
{
	mayday = vmalloc(PAGE_SIZE);
	if (mayday == NULL)
		return -ENOMEM;

	setup_mayday(mayday);

	return 0;
}

void xnarch_cleanup_mayday(void)
{
	vfree(mayday);
}

void *xnarch_get_mayday_page(void)
{
	return mayday;
}

void xnarch_handle_mayday(struct xnarchtcb *tcb,
			  struct pt_regs *regs, unsigned long tramp)
{
	tcb->mayday.ea = regs->ea;
	tcb->mayday.r2 = regs->r2;
	tcb->mayday.r3 = regs->r3;
	regs->ea = tramp;
}

void xnarch_fixup_mayday(struct xnarchtcb *tcb, struct pt_regs *regs)
{
	regs->ea = tcb->mayday.ea;
	regs->r2 = tcb->mayday.r2;
	regs->r3 = tcb->mayday.r3;
}
