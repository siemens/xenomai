/*
 * Copyright (C) 2009 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#ifndef _LIB_COBALT_CURRENT_H
#define _LIB_COBALT_CURRENT_H

#include <stdint.h>
#include <pthread.h>
#include <cobalt/uapi/thread.h>
#include <xeno_config.h>

extern pthread_key_t cobalt_current_window_key;

xnhandle_t cobalt_get_current_slow(void);

#ifdef HAVE_TLS
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
xnhandle_t cobalt_current;
extern __thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct xnthread_user_window *cobalt_current_window;

static inline xnhandle_t cobalt_get_current(void)
{
	return cobalt_current;
}

static inline xnhandle_t cobalt_get_current_fast(void)
{
	return cobalt_get_current();
}

static inline int cobalt_get_current_mode(void)
{
	return cobalt_current_window ? cobalt_current_window->state : XNRELAX;
}

static inline struct xnthread_user_window *cobalt_get_current_window(void)
{
	return cobalt_current ? cobalt_current_window : NULL;
}

#else /* ! HAVE_TLS */
extern pthread_key_t cobalt_current_key;

xnhandle_t cobalt_get_current_slow(void);

static inline xnhandle_t cobalt_get_current(void)
{
	void *val = pthread_getspecific(cobalt_current_key);

	return (xnhandle_t)(uintptr_t)val ?: cobalt_get_current_slow();
}

/* syscall-free, but unreliable in TSD destructor context */
static inline xnhandle_t cobalt_get_current_fast(void)
{
	void *val = pthread_getspecific(cobalt_current_key);

	return (xnhandle_t)(uintptr_t)val ?: XN_NO_HANDLE;
}

static inline int cobalt_get_current_mode(void)
{
	struct xnthread_user_window *window;

	window = pthread_getspecific(cobalt_current_window_key);

	return window ? window->state : XNRELAX;
}

static inline struct xnthread_user_window *cobalt_get_current_window(void)
{
	return pthread_getspecific(cobalt_current_window_key);
}

#endif /* ! HAVE_TLS */

void cobalt_init_current_keys(void);

void cobalt_set_tsd(__u32 u_winoff);

void cobalt_clear_tsd(void);

#endif /* _LIB_COBALT_CURRENT_H */
