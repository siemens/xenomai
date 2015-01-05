/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _LIB_COBALT_INTERNAL_H
#define _LIB_COBALT_INTERNAL_H

#include <cobalt/sys/cobalt.h>
#include "current.h"

extern void *cobalt_umm_private;

extern void *cobalt_umm_shared;

void cobalt_sigshadow_install_once(void);

static inline
struct cobalt_mutex_state *mutex_get_state(struct cobalt_mutex_shadow *shadow)
{
	if (shadow->attr.pshared)
		return cobalt_umm_shared + shadow->state_offset;

	return cobalt_umm_private + shadow->state_offset;
}

static inline atomic_t *mutex_get_ownerp(struct cobalt_mutex_shadow *shadow)
{
	return &mutex_get_state(shadow)->owner;
}

void cobalt_thread_init(void);

void cobalt_print_init(void);

void cobalt_print_init_atfork(void);

void cobalt_print_exit(void);

void cobalt_ticks_init(unsigned long long freq);

void cobalt_default_mutexattr_init(void);

void cobalt_default_condattr_init(void);

struct cobalt_featinfo;

void cobalt_check_features(struct cobalt_featinfo *finfo);

extern pthread_t __cobalt_main_ptid;

extern struct sigaction __cobalt_orig_sigdebug;

#endif /* _LIB_COBALT_INTERNAL_H */
