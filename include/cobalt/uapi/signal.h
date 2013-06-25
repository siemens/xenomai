/*
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_SIGNAL_H
#define _COBALT_UAPI_SIGNAL_H

/*
 * Those are pseudo-signals only available with pthread_kill() to
 * suspend/resume/unblock threads synchronously, force them out of
 * primary mode or even demote them to the SCHED_OTHER class via the
 * low-level nucleus interface. Can't block those signals, queue them,
 * or even set them in a sigset. Those are nasty, strictly anti-POSIX
 * things; we do provide them nevertheless only because we are mean
 * people doing harmful code for no valid reason. Can't go against
 * your nature, right?  Nah... (this said, don't blame us for POSIX,
 * we are not _that_ mean).
 */
#define SIGSUSP (SIGRTMAX + 1)
#define SIGRESM (SIGRTMAX + 2)
#define SIGRELS (SIGRTMAX + 3)
#define SIGKICK (SIGRTMAX + 4)
#define SIGDEMT (SIGRTMAX + 5)

#endif /* !_COBALT_UAPI_SIGNAL_H */
