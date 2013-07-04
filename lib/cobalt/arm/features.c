/*
 * Copyright (C) 2011 Gilles Chanteperdrix <gch@xenomai.org>.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <cobalt/wrappers.h>
#include <xenomai/syscall.h>
#include <xenomai/tsc.h>
#include "internal.h"

struct __xn_full_tscinfo __xn_tscinfo = {
	.kinfo = {
		.type = -1,
	},
};

void cobalt_check_features(struct xnfeatinfo *finfo)
{
#ifdef CONFIG_XENO_ARM_TSC_TYPE
	unsigned long phys_addr;
	unsigned page_size;
	int err, fd;
	void *addr;

	if (__xn_tscinfo.kinfo.type != -1)
		return;

	err = XENOMAI_SYSCALL2(sc_nucleus_arch,
			       XENOMAI_SYSARCH_TSCINFO, &__xn_tscinfo);
	if (err)
		goto error;

	fd = __STD(open("/dev/mem", O_RDONLY | O_SYNC));
	if (fd == -1) {
		report_error("open(/dev/mem): %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	page_size = sysconf(_SC_PAGESIZE);

	switch(__xn_tscinfo.kinfo.type) {
#if CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_KUSER
	default:
		__xn_tscinfo.kuser_tsc_get =
			(__xn_rdtsc_t *)(0xffff1004 -
					 ((*(unsigned *)(0xffff0ffc) + 3) << 5));
		goto domap;

#elif CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_FREERUNNING		\
	|| CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_FREERUNNING_COUNTDOWN \
	|| CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_FREERUNNING_FAST_WRAP
	case __XN_TSC_TYPE_FREERUNNING:
	case __XN_TSC_TYPE_FREERUNNING_COUNTDOWN:
#if CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_FREERUNNING_FAST_WRAP
		if (__xn_tscinfo.kinfo.mask >= ((1 << 28) - 1)) {
			report_error("Hardware TSC is not a fast wrapping "
				     "one, select the correct platform, or fix\n"
				     "configure.in");
			exit(EXIT_FAILURE);
		}
#endif /* __XN_TSC_TYPE_FREERUNNING_FAST_WRAP */
		goto domap;

	default:
		report_error("kernel/user TSC emulation mismatch");
		exit(EXIT_FAILURE);
		break;
#elif CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_DECREMENTER
	case __XN_TSC_TYPE_DECREMENTER:
		goto domap;

	default:
		report_error("kernel/user TSC emulation mismatch");
		exit(EXIT_FAILURE);
		break;
#endif /* CONFIG_XENO_ARM_TSC_TYPE == __XN_TSC_TYPE_DECREMENTER */
	case __XN_TSC_TYPE_NONE:
	  error:
		report_error("Your board/configuration does not "
			     "allow TSC emulation in user-space: %s ",
			     strerror(-err));
		exit(EXIT_FAILURE);
	}

  domap:
	phys_addr = (unsigned long) __xn_tscinfo.kinfo.counter;

	addr = mmap(NULL, page_size, PROT_READ, MAP_SHARED,
		    fd, phys_addr & ~(page_size - 1));
	if (addr == MAP_FAILED) {
		report_error("mmap(/dev/mem): %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	__xn_tscinfo.kinfo.counter =
		((volatile unsigned *)
		 ((char *) addr + (phys_addr & (page_size - 1))));

	__STD(close(fd));
#endif /* CONFIG_XENO_ARM_TSC_TYPE */
}
