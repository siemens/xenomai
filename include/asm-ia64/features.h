/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_IA64_FEATURES_H
#define _XENO_ASM_IA64_FEATURES_H

#include <asm-generic/xenomai/features.h>

/* The ABI revision level we use on this arch. */
#define XENOMAI_ABI_REV   1

#define XENOMAI_FEAT_DEP  __xn_feat_generic_mask

static inline int check_feature_dependencies(unsigned long featdep)
{
    const unsigned long featreq = 0;
    return (XENOMAI_FEAT_DEP & featreq) == (featdep & featreq);
}

static inline int check_abi_revision(unsigned long abirev)
{
    return abirev == XENOMAI_ABI_REV;
}

#endif /* !_XENO_ASM_IA64_FEATURES_H */
