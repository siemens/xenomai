/**
 *   Copyright &copy; 2011 Gilles Chanteperdrix.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *   Generic atomic operations.
 */
#ifndef _COBALT_ASM_GENERIC_ATOMIC_H
#define _COBALT_ASM_GENERIC_ATOMIC_H

#ifndef __KERNEL__

#include <xeno_config.h>

typedef struct {
	unsigned long v;
} atomic_long_t;

#ifndef cpu_relax
#define cpu_relax() __sync_synchronize()
#endif

#ifndef atomic_long_read
#define atomic_long_read(p)		((p)->v)
#endif

#ifndef atomic_long_set
#define atomic_long_set(p, i)		((p)->v = i)
#endif

#ifndef atomic_long_cmpxchg
#define atomic_long_cmpxchg(p, o, n)				\
	__sync_val_compare_and_swap(&(p)->v,			\
				    (typeof((p)->v))(o),	\
				    (typeof((p)->v))(n))
#endif

#ifndef smp_mb
#define smp_mb() __sync_synchronize()
#endif

#ifndef smp_rmb
#define smp_rmb() __sync_synchronize()
#endif

#ifndef smp_wmb
#define smp_wmb() __sync_synchronize()
#endif

#endif /* !__KERNEL__ */

#endif /* _COBALT_ASM_GENERIC_ATOMIC_H */
