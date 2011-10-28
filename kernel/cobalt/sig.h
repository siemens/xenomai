/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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


#ifndef _POSIX_SIG_H
#define _POSIX_SIG_H

#include "thread.h"

#define SIGACTION_FLAGS (SA_ONESHOT|SA_NOMASK|SA_SIGINFO)

typedef struct {
    siginfo_t info;
    xnpholder_t link;

#define link2siginfo(iaddr) \
    ((cobalt_siginfo_t *)(((char *)iaddr) - offsetof(cobalt_siginfo_t, link)))

} cobalt_siginfo_t;

/* Must be called with nklock lock, irqs off returns non zero if rescheduling
   needed. */
int cobalt_sigqueue_inner(pthread_t thread, cobalt_siginfo_t *si);

void cobalt_sigunqueue(pthread_t thread, cobalt_siginfo_t *si);

void cobalt_signal_init_thread(pthread_t new, const pthread_t parent);

void cobalt_signal_cleanup_thread(pthread_t zombie);

void cobalt_signal_handle_request(pthread_t thread);

void cobalt_signal_pkg_init(void);

void cobalt_signal_pkg_cleanup(void);

#endif /* !_POSIX_SIG_H */
