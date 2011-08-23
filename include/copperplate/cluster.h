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

#ifndef _COPPERPLATE_CLUSTER_H
#define _COPPERPLATE_CLUSTER_H

#include <copperplate/init.h>
#include <copperplate/hash.h>

#ifdef CONFIG_XENO_PSHARED

struct clusterobj {
	pid_t cnode;
	struct hashobj hobj;
};

struct dictionary {
	struct hash_table table;
	struct hashobj hobj;
};

struct cluster {
	struct dictionary *d;
};

struct pvclusterobj {
	struct pvhashobj hobj;
};

struct pvcluster {
	struct pvhash_table table;
};

#else /* !CONFIG_XENO_PSHARED */

struct clusterobj {
	struct pvhashobj hobj;
};

struct cluster {
	struct pvhash_table table;
};

#define pvclusterobj  clusterobj
#define pvcluster     cluster

#endif /* !CONFIG_XENO_PSHARED */

#ifdef __cplusplus
extern "C" {
#endif

int pvcluster_init(struct pvcluster *c, const char *name);

void pvcluster_destroy(struct pvcluster *c);

int pvcluster_addobj(struct pvcluster *c, const char *name,
		     struct pvclusterobj *cobj);

int pvcluster_delobj(struct pvcluster *c,
		     struct pvclusterobj *cobj);

struct pvclusterobj *pvcluster_findobj(struct pvcluster *c,
				       const char *name);
#ifdef CONFIG_XENO_PSHARED

int cluster_init(struct cluster *c, const char *name);

int cluster_addobj(struct cluster *c, const char *name,
		   struct clusterobj *cobj);

int cluster_delobj(struct cluster *c,
		   struct clusterobj *cobj);

struct clusterobj *cluster_findobj(struct cluster *c,
				   const char *name);
#else /* !CONFIG_XENO_PSHARED */

static inline int cluster_init(struct cluster *c, const char *name)
{
	return pvcluster_init(c, name);
}

static inline int cluster_addobj(struct cluster *c, const char *name,
				 struct clusterobj *cobj)
{
	return pvcluster_addobj(c, name, cobj);
}

static inline int cluster_delobj(struct cluster *c,
				 struct clusterobj *cobj)
{
	return pvcluster_delobj(c, cobj);
}

static inline struct clusterobj *cluster_findobj(struct cluster *c,
						 const char *name)
{
	return pvcluster_findobj(c, name);
}

#endif /* !CONFIG_XENO_PSHARED */

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_CLUSTER_H */
