/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _XENO_VRTX_DEFS_H
#define _XENO_VRTX_DEFS_H

#include <nucleus/xenomai.h>
#include <nucleus/registry.h>
#include <vrtx/vrtx.h>

#define VRTX_MAX_TID 512	/* i.e. 1 - 511 */
#define VRTX_MAX_PID 32  	/* i.e. 0 - 31 */
#define VRTX_MAX_QID 256  	/* i.e. 0 - 255 */
#define VRTX_MAX_MXID 256  	/* i.e. 0 - 255 */
#define VRTX_MAX_CB  256 	/* i.e. 0 - 255 */

#define vrtx_h2obj_active(h,m,t) \
((h) && ((t *)(h))->magic == (m) ? ((t *)(h)) : NULL)

#define vrtx_mark_deleted(t) ((t)->magic = ~(t)->magic)

#ifdef __cplusplus
extern "C" {
#endif

int vrtx_alloc_id(void *refobject);

void vrtx_release_id(int id);

void *vrtx_find_object_by_id(int id);

#ifdef __cplusplus
}
#endif

#endif /* !_XENO_VRTX_DEFS_H */
