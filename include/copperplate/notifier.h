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

#ifndef _COPPERPLATE_NOTIFIER_H
#define _COPPERPLATE_NOTIFIER_H

#include <boilerplate/list.h>

struct notifier {
	pid_t owner;
	struct pvholder link;
};

#ifdef __cplusplus
extern "C" {
#endif

int notifier_init(struct notifier *nf, pid_t pid);

static inline void notifier_destroy(struct notifier *nf)
{
}

void notifier_destroy(struct notifier *nf);

void notifier_signal(struct notifier *nf);

void notifier_wait(void);

void notifier_disable(struct notifier *nf);

void notifier_release(struct notifier *nf);

void notifier_pkg_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_NOTIFIER_H */
