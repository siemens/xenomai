/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

	.align	2
	.globl rthal_thread_trampoline
	.balign	32
rthal_thread_trampoline:
	mov.l	@r15+, r10
	mov.l	@r15+, r9
	mov.l	@r15+, r4
	ldc	r9, sr
	mov.l	1f, r9
	jmp	@r9
	 ldc	r10, gbr
	.balign	4
1:	.long	xnarch_thread_trampoline
