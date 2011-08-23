/**
 * @file
 * Analogy for Linux, root / leaf system
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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

#ifndef __ANALOGY_ROOT_LEAF_H__
#define __ANALOGY_ROOT_LEAF_H__

#ifndef DOXYGEN_CPP

#include <errno.h>

struct a4l_leaf {
	unsigned int id;
	unsigned int nb_leaf;
	struct a4l_leaf *lfnxt;
	struct a4l_leaf *lfchd;
	void *data;
};
typedef struct a4l_leaf a4l_leaf_t;

struct a4l_root {
	/* Same fields as a4l_leaf_t */
	unsigned int id;
	unsigned int nb_leaf;
	struct a4l_leaf *lfnxt;
	struct a4l_leaf *lfchd;
	void *data;
	/* Root specific: buffer control stuff */
	void *offset;
	unsigned long gsize;
};
typedef struct a4l_root a4l_root_t;

#endif /* !DOXYGEN_CPP */

#endif /* __ANALOGY_ROOT_LEAF_H__ */
