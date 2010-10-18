/*
 * Two Levels Segregate Fit memory allocator (TLSF)
 * Version 2.4
 *
 * Written by Miguel Masmano Tello <mimastel@doctor.upv.es>
 *
 * Thanks to Ismael Ripoll for his suggestions and reviews
 *
 * Copyright (C) 2008, 2007, 2006, 2005, 2004
 *
 * This code is released using a dual license strategy: GPL/LGPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of the GNU General Public License Version 2.0
 * Released under the terms of the GNU Lesser General Public License Version 2.1
 *
 */

#ifndef _TLSF_H_
#define _TLSF_H_

#include <sys/types.h>

size_t init_memory_pool(size_t, void *);
void destroy_memory_pool(void *);
size_t add_new_area(void *, size_t, void *);
void *malloc_ex(size_t, void *);
void free_ex(void *, void *);
void *realloc_ex(void *, size_t, void *);
void *calloc_ex(size_t, size_t, void *);
size_t malloc_usable_size_ex(void *ptr, void *mem_pool);

void *tlsf_malloc(size_t size);
void tlsf_free(void *ptr);
void *tlsf_realloc(void *ptr, size_t size);
void *tlsf_calloc(size_t nelem, size_t elem_size);

#endif
