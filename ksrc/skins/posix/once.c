/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

/**
 * @addtogroup posix_thread
 *
 *@{*/

#include <posix/internal.h>

/**
 * Execute an initialization routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_once.html">
 * Specification.</a>
 * 
 */
int pthread_once (pthread_once_t *once, void (*init_routine)(void))

{
    unsigned routine_called;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(once,PSE51_ONCE_MAGIC,pthread_once_t) || !init_routine)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    routine_called = once->routine_called;
    if (!routine_called)
        once->routine_called = 1;

    xnlock_put_irqrestore(&nklock, s);
    
    if (!routine_called)
        init_routine();

    return 0;    
}

/*@}*/

EXPORT_SYMBOL(pthread_once);
