/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "copperplate/reference.h"
#include "copperplate/panic.h"

unsigned int __hash_key(const void *key,
			int length, unsigned int c);

static int nrefs[MAX_FNLIBS];

struct __fnref __fnrefs[MAX_FNLIBS][MAX_FNREFS] = {
	{ { NULL, -1U } }
};

int __fnref_register(const char *libname,
		     int libtag, int cbirev,
		     const char *symname, void (*fn)(void))
{
	unsigned int hash;
	size_t len;
	int pos;

	if ((unsigned int)libtag >= MAX_FNLIBS)
		panic("reference table overflow for library %s",
		      libname);

	pos = nrefs[libtag]++;
	if (pos >= MAX_FNREFS)
		panic("too many function references in library %s (> %d)",
		      libname, MAX_FNREFS);

	assert(__fnrefs[libtag][pos].fn == NULL);
	__fnrefs[libtag][pos].fn = fn;
	len = strlen(symname);
	hash = __hash_key(symname, len, 0);
	hash = __hash_key(&cbirev, sizeof(cbirev), hash);
	__fnrefs[libtag][pos].hash = hash & 0xfffff;

	return __refmangle(libtag, hash, pos);
}
