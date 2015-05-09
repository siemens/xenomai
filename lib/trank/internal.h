/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _TRANK_INTERNAL_H
#define _TRANK_INTERNAL_H

#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <xeno_config.h>

struct trank_context {
	timer_t periodic_timer;
};

#ifdef HAVE_TLS
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct trank_context trank_context;

static inline struct trank_context *trank_get_context(void)
{
	return &trank_context;
}

#else

extern pthread_key_t trank_context_key;

static inline struct trank_context *trank_get_context(void)
{
	return pthread_getspecific(trank_context_key);
}

#endif

int trank_init_interface(void);

extern sigset_t trank_sigperiod_set;

#endif /* !_TRANK_INTERNAL_H */
