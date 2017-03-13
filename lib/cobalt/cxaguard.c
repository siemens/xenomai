/*
 * Copyright (C) 2017 Henning Schild <henning.schild@siemens.com>
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
#include <stdint.h>
#include <cobalt/sys/cobalt.h>
#include <cobalt/wrappers.h>

#ifdef __ARM_EABI__
typedef uint32_t __cxa_guard_type;
#else
typedef uint64_t __cxa_guard_type;
#endif

COBALT_DECL(int, __cxa_guard_acquire(__cxa_guard_type *g));
int __real_cxa_guard_acquire(__cxa_guard_type *g);

/* CXXABI 3.3.2 One-time Construction API */
COBALT_IMPL(int, __cxa_guard_acquire, (__cxa_guard_type *g))
{
	cobalt_assert_nrt();
	return __STD(__cxa_guard_acquire(g));
}

COBALT_DECL(void, __cxa_guard_release(__cxa_guard_type *g));
void __real_cxa_guard_release(__cxa_guard_type *g);

COBALT_IMPL(void, __cxa_guard_release, (__cxa_guard_type *g))
{
	cobalt_assert_nrt();
	__STD(__cxa_guard_release(g));
}

COBALT_DECL(void, __cxa_guard_abort(__cxa_guard_type *g));
void __real_cxa_guard_abort(__cxa_guard_type *g);

COBALT_IMPL(void, __cxa_guard_abort, (__cxa_guard_type *g))
{
	cobalt_assert_nrt();
	__STD(__cxa_guard_abort(g));
}
