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

/**
 * @addtogroup posix_thread
 *
 *@{*/

#include <posix/internal.h>

static pthread_mutex_t mutex;
static pthread_cond_t cond;

/**
 * Execute an initialization routine.
 *
 * This service may be used by libraries which need an initialization function
 * to be called only once.
 *
 * The function @a init_routine will only be called, with no argument, the first
 * time this service is called specifying the address @a once.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, the object pointed to by @a once is invalid (it must have been
 *   initialized with PTHREAD_ONCE_INIT);
 * - EPERM, the caller context is invalid.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_once.html">
 * Specification.</a>
 *
 */

enum init_step {
	UNINIT = 0,
	INIT_STARTED,
	INIT_DONE,
};

static void once_rollback(void *cookie)
{
	pthread_once_t *once = cookie;

	pthread_mutex_lock(&mutex);
	once->init_step = UNINIT;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
}

int pthread_once(pthread_once_t *once, void (*init_routine)(void))
{
	int err;

	if (pse51_obj_active(once, PSE51_ONCE_MAGIC, pthread_once_t) &&
		once->init_step == INIT_DONE)
		return 0;

	err = pthread_mutex_lock(&mutex);
	if (err)
		return err;

	if (!pse51_obj_active(once, PSE51_ONCE_MAGIC, pthread_once_t)) {
		err = EINVAL;
		goto out;
	}

	while (once->init_step != INIT_DONE)
		switch (once->init_step) {
		case UNINIT:
			once->init_step = INIT_STARTED;
			pthread_mutex_unlock(&mutex);

			pthread_cleanup_push(once_rollback, once);
			init_routine();
			pthread_cleanup_pop(0);

			pthread_mutex_lock(&mutex);
			once->init_step = INIT_DONE;
			pthread_cond_broadcast(&cond);
			break;

		case INIT_STARTED:
			err = pthread_cond_wait(&cond, &mutex);
			if (err)
				goto out;
			break;

		default:
			err = EINVAL;
			goto out;
		}

  out:
	pthread_mutex_unlock(&mutex);

	return err;
}

int pse51_once_pkg_init(void)
{
	pthread_mutexattr_t tattr;
	int err;

	err = pthread_mutexattr_init(&tattr);
	if (err) {
		printk("Posix: pthread_once/pthread_mutexattr_init: %d\n", err);
		return err;
	}

	err = pthread_mutexattr_setprotocol(&tattr, PTHREAD_PRIO_INHERIT);
	if (err) {
		printk("Posix: pthread_once/set_protocol: %d\n", err);
		return err;
	}

	err = pthread_mutex_init(&mutex, &tattr);
	if (err) {
		printk("Posix: pthread_once/mutex_init: %d\n", err);
		return err;
	}

	err = pthread_cond_init(&cond, NULL);
	if (err) {
		printk("Posix: pthread_once/cond_init: %d\n", err);
		pthread_mutex_destroy(&mutex);
	}

	return err;
}

void pse51_once_pkg_cleanup(void)
{
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

/*@}*/

EXPORT_SYMBOL_GPL(pthread_once);
