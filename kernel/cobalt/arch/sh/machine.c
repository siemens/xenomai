/**
 *   Copyright (C) 2011 Philippe Gerum.
 *
 *   Xenomai is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   Xenomai is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */
#include <linux/stddef.h>
#include <asm/xenomai/machine.h>

static unsigned long mach_sh_calibrate(void)
{
	unsigned long flags;
	u64 t, v;
	int n;

	flags = hard_local_irq_save();

	ipipe_read_tsc(t);

	barrier();

	for (n = 1; n <= 100; n++)
		ipipe_read_tsc(v);

	hard_local_irq_restore(flags);

	return xnarch_ulldiv(v - t, n, NULL);
}

static const char *const fault_labels[] = {
	[0] = "Breakpoint",
	[1] = "Page fault",
	[2] = "Address error",
	[3] = "FPU error",
	[4] = "Exception error",
	[5] = NULL
};

struct cobalt_machine cobalt_machine = {
	.name = "sh",
	.init = NULL,
	.cleanup = NULL,
	.calibrate = mach_sh_calibrate,
	.prefault = NULL,
	.fault_labels = fault_labels,
};
