/**
 * @file
 * @note Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup arith
 */

#ifndef _XENO_NUCLEUS_ARITH_H
#define _XENO_NUCLEUS_ARITH_H

/** @addtogroup arith
 *@{*/

unsigned long long xnarch_generic_full_divmod64(unsigned long long a,
						 unsigned long long b,
						 unsigned long long *rem);

/*@}*/

#endif /* !_XENO_NUCLEUS_ARITH_H */
