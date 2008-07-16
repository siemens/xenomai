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

#include <native/syscall.h>
#include <native/buffer.h>

extern int __native_muxid;

int rt_buffer_create(RT_BUFFER *bf, const char *name, size_t bufsz, int mode)
{
	return XENOMAI_SKINCALL4(__native_muxid,
				 __native_buffer_create, bf, name, bufsz,
				 mode);
}

int rt_buffer_bind(RT_BUFFER *bf, const char *name, RTIME timeout)
{
	return XENOMAI_SKINCALL3(__native_muxid,
				 __native_buffer_bind, bf, name, &timeout);
}

int rt_buffer_delete(RT_BUFFER *buffer)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_buffer_delete, buffer);
}

ssize_t rt_buffer_read(RT_BUFFER *bf, void *buf, size_t size, RTIME timeout)
{
	return XENOMAI_SKINCALL5(__native_muxid,
				 __native_buffer_read, bf, buf, size,
				 XN_RELATIVE, &timeout);
}

ssize_t rt_buffer_read_until(RT_BUFFER *bf, void *buf, size_t size, RTIME timeout)
{
	return XENOMAI_SKINCALL5(__native_muxid,
				 __native_buffer_read, bf, buf, size,
				 XN_REALTIME, &timeout);
}

ssize_t rt_buffer_write(RT_BUFFER *bf, const void *buf, size_t size, RTIME timeout)
{
	return XENOMAI_SKINCALL5(__native_muxid,
				 __native_buffer_write, bf, buf, size,
				 XN_RELATIVE, &timeout);
}

ssize_t rt_buffer_write_until(RT_BUFFER *bf, const void *buf, size_t size, RTIME timeout)
{
	return XENOMAI_SKINCALL5(__native_muxid,
				 __native_buffer_write, bf, buf, size,
				 XN_REALTIME, &timeout);
}

int rt_buffer_clear(RT_BUFFER *bf)
{
	return XENOMAI_SKINCALL1(__native_muxid,
				 __native_buffer_clear, bf);
}

int rt_buffer_inquire(RT_BUFFER *bf, RT_BUFFER_INFO *info)
{
	return XENOMAI_SKINCALL2(__native_muxid, __native_buffer_inquire, bf,
				 info);
}
