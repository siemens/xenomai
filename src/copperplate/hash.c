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

/*
 * We need hash table management with a removal op, so we can't rely
 * on <search.h>.
 */

#include <string.h>
#include <errno.h>
#include "copperplate/lock.h"
#include "copperplate/hash.h"

/*
 * Crunching routine borrowed from:
 *
 * lookup2.c, by Bob Jenkins, December 1996, Public Domain.
 * hash(), hash2(), hash3, and mix() are externally useful functions.
 * Routines to test the hash are included if SELF_TEST is defined.
 * You can use this free for any purpose.  It has no warranty.
 */

#define __mixer(a, b, c) \
	{					\
	a -= b; a -= c; a ^= (c>>13);		\
	b -= c; b -= a; b ^= (a<<8);		\
	c -= a; c -= b; c ^= (b>>13);		\
	a -= b; a -= c; a ^= (c>>12);		\
	b -= c; b -= a; b ^= (a<<16);		\
	c -= a; c -= b; c ^= (b>>5);		\
	a -= b; a -= c; a ^= (c>>3);		\
	b -= c; b -= a; b ^= (a<<10);		\
	c -= a; c -= b; c ^= (b>>15);		\
}

#define GOLDEN_HASH_RATIO  0x9e3779b9  /* Arbitrary value. */

unsigned int __hash_key(const void *key, int length, unsigned int c)
{
	const unsigned char *k = key;
	unsigned int a, b, len;

	len = length;
	a = b = GOLDEN_HASH_RATIO;

	while (len >= 12) {
		a += (k[0] +((unsigned int)k[1]<<8) +((unsigned int)k[2]<<16) +((unsigned int)k[3]<<24));
		b += (k[4] +((unsigned int)k[5]<<8) +((unsigned int)k[6]<<16) +((unsigned int)k[7]<<24));
		c += (k[8] +((unsigned int)k[9]<<8) +((unsigned int)k[10]<<16)+((unsigned int)k[11]<<24));
		__mixer(a, b, c);
		k += 12;
		len -= 12;
	}

	c += length;

	switch (len) {
	case 11: c += ((unsigned int)k[10]<<24);
	case 10: c += ((unsigned int)k[9]<<16);
	case 9 : c += ((unsigned int)k[8]<<8);
	case 8 : b += ((unsigned int)k[7]<<24);
	case 7 : b += ((unsigned int)k[6]<<16);
	case 6 : b += ((unsigned int)k[5]<<8);
	case 5 : b += k[4];
	case 4 : a += ((unsigned int)k[3]<<24);
	case 3 : a += ((unsigned int)k[2]<<16);
	case 2 : a += ((unsigned int)k[1]<<8);
	case 1 : a += k[0];
	};

	__mixer(a, b, c);

	return c;
}

void __hash_init(void *heap, struct hash_table *t)
{
	pthread_mutexattr_t mattr;
	int n;

	for (n = 0; n < HASHSLOTS; n++)
		__list_init(heap, &t->table[n].obj_list);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	pthread_mutex_init(&t->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
}

void hash_destroy(struct hash_table *t)
{
	pthread_mutex_destroy(&t->lock);
}

static struct hash_bucket *do_hash(struct hash_table *t, const char *key)
{
	unsigned int hash = __hash_key(key, strlen(key), 0);
	return &t->table[hash & (HASHSLOTS-1)];
}

int hash_enter(struct hash_table *t,
	       const char *key, struct hashobj *newobj)
{
	struct hash_bucket *bucket;
	struct hashobj *obj;
	int ret = 0;

	holder_init(&newobj->link);
	newobj->key = key;
	bucket = do_hash(t, key);

	write_lock_nocancel(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (strcmp(obj->key, key) == 0) {
				ret = -EBUSY;
				goto out;
			}
		}
	}

	list_prepend(&newobj->link, &bucket->obj_list);
out:
	write_unlock(&t->lock);

	return ret;
}

int hash_remove(struct hash_table *t, struct hashobj *delobj)
{
	struct hash_bucket *bucket;
	struct hashobj *obj;
	int ret = -ESRCH;

	bucket = do_hash(t, delobj->key);

	write_lock_nocancel(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj == delobj) {
				list_remove_init(&obj->link);
				ret = 0;
				goto out;
			}
		}
	}
out:
	write_unlock(&t->lock);

	return ret;
}

struct hashobj *hash_search(struct hash_table *t, const char *key)
{
	struct hash_bucket *bucket;
	struct hashobj *obj;

	bucket = do_hash(t, key);

	read_lock_nocancel(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (strcmp(obj->key, key) == 0)
				goto out;
		}
	}
	obj = NULL;
out:
	read_unlock(&t->lock);

	return obj;
}

#ifdef CONFIG_XENO_PSHARED

int hash_enter_probe(struct hash_table *t,
		     const char *key, struct hashobj *newobj,
		     int (*probefn)(struct hashobj *oldobj))
{
	struct hash_bucket *bucket;
	struct hashobj *obj;
	int ret = 0;

	holder_init(&newobj->link);
	newobj->key = key;
	bucket = do_hash(t, key);

	push_cleanup_lock(&t->lock);
	write_lock(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (strcmp(obj->key, key) == 0) {
				if (probefn(obj)) {
					ret = -EBUSY;
					goto out;
				}
				list_remove_init(&obj->link);
				break;
			}
		}
	}

	list_prepend(&newobj->link, &bucket->obj_list);
out:
	write_unlock(&t->lock);
	pop_cleanup_lock(&t->lock);

	return ret;
}

struct hashobj *hash_search_probe(struct hash_table *t, const char *key,
				  int (*probefn)(struct hashobj *obj))
{
	struct hash_bucket *bucket;
	struct hashobj *obj;

	bucket = do_hash(t, key);

	push_cleanup_lock(&t->lock);
	write_lock(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (strcmp(obj->key, key) == 0) {
				if (!probefn(obj)) {
					list_remove_init(&obj->link);
					goto fail;
				}
				goto out;
			}
		}
	}
fail:
	obj = NULL;
out:
	write_unlock(&t->lock);
	pop_cleanup_lock(&t->lock);

	return obj;
}

void pvhash_init(struct pvhash_table *t)
{
	pthread_mutexattr_t mattr;
	int n;

	for (n = 0; n < HASHSLOTS; n++)
		pvlist_init(&t->table[n].obj_list);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutex_init(&t->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
}

static struct pvhash_bucket *do_pvhash(struct pvhash_table *t, const char *key)
{
	unsigned int hash = __hash_key(key, strlen(key), 0);
	return &t->table[hash & (HASHSLOTS-1)];
}

int pvhash_enter(struct pvhash_table *t,
		 const char *key, struct pvhashobj *newobj)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj;
	int ret = 0;

	pvholder_init(&newobj->link);
	newobj->key = key;
	bucket = do_pvhash(t, key);

	write_lock_nocancel(&t->lock);

	if (!pvlist_empty(&bucket->obj_list)) {
		pvlist_for_each_entry(obj, &bucket->obj_list, link) {
			if (strcmp(obj->key, key) == 0) {
				ret = -EBUSY;
				goto out;
			}
		}
	}

	pvlist_prepend(&newobj->link, &bucket->obj_list);
out:
	write_unlock(&t->lock);

	return ret;
}

int pvhash_remove(struct pvhash_table *t, struct pvhashobj *delobj)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj;
	int ret = -ESRCH;

	bucket = do_pvhash(t, delobj->key);

	write_lock_nocancel(&t->lock);

	if (!pvlist_empty(&bucket->obj_list)) {
		pvlist_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj == delobj) {
				pvlist_remove_init(&obj->link);
				ret = 0;
				goto out;
			}
		}
	}
out:
	write_unlock(&t->lock);

	return ret;
}

struct pvhashobj *pvhash_search(struct pvhash_table *t, const char *key)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj;

	bucket = do_pvhash(t, key);

	read_lock_nocancel(&t->lock);

	if (!pvlist_empty(&bucket->obj_list)) {
		pvlist_for_each_entry(obj, &bucket->obj_list, link) {
			if (strcmp(obj->key, key) == 0)
				goto out;
		}
	}
	obj = NULL;
out:
	read_unlock(&t->lock);

	return obj;
}

#endif /* CONFIG_XENO_PSHARED */
