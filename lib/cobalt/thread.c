/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <semaphore.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"
#include <boilerplate/ancillaries.h>

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_thread Thread management
 *
 * Cobalt/POSIX thread management services
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_09.html#tag_02_09">
 * Specification.</a>
 *
 *@{
 */

static pthread_attr_ex_t default_attr_ex;

static int linuxthreads;

static int std_maxpri;

static void commit_stack_memory(void)
{
	if (pthread_self() == __cobalt_main_ptid) {
		char stk[cobalt_get_stacksize(1)];
		cobalt_commit_memory(stk);
	}
}

static int libc_setschedparam(pthread_t thread,
			      int policy, const struct sched_param_ex *param_ex)
{
	struct sched_param param;
	int priority;

	priority = param_ex->sched_priority;

	switch (policy) {
	case SCHED_WEAK:
		policy = priority ? SCHED_FIFO : SCHED_OTHER;
		break;
	default:
		policy = SCHED_FIFO;
		/* falldown wanted. */
	case SCHED_OTHER:
	case SCHED_FIFO:
	case SCHED_RR:
		/*
		 * The Cobalt priority range is larger than those of
		 * the native SCHED_FIFO/RR classes, so we have to cap
		 * the priority value accordingly.  We also remap
		 * "weak" (negative) priorities - which are only
		 * meaningful for the Cobalt core - to regular values.
		 */
		if (priority > std_maxpri)
			priority = std_maxpri;
		else if (priority < 0)
			priority = -priority;
	}

	memset(&param, 0, sizeof(param));
	param.sched_priority = priority;

	return __STD(pthread_setschedparam(thread, policy, &param));
}

struct pthread_iargs {
	struct sched_param_ex param_ex;
	int policy;
	int personality;
	void *(*start)(void *);
	void *arg;
	int parent_prio;
	sem_t sync;
	int ret;
};

static void *cobalt_thread_trampoline(void *p)
{
	/*
	 * Volatile is to prevent (too) smart gcc releases from
	 * trashing the syscall registers (see later comment).
	 */
	volatile pthread_t ptid = pthread_self();
	void *(*start)(void *), *arg, *retval;
	int personality, parent_prio, policy;
	struct pthread_iargs *iargs = p;
	struct sched_param_ex param_ex;
	__u32 u_winoff;
	long ret;

	cobalt_sigshadow_install_once();
	commit_stack_memory();

	personality = iargs->personality;
	param_ex = iargs->param_ex;
	policy = iargs->policy;
	parent_prio = iargs->parent_prio;
	start = iargs->start;
	arg = iargs->arg;

	/* Set our scheduling parameters for the host kernel first. */
	ret = libc_setschedparam(ptid, policy, &param_ex);
	if (ret)
		goto sync_with_creator;

	/*
	 * Do _not_ inline the call to pthread_self() in the syscall
	 * macro: this trashes the syscall regs on some archs.
	 */
	ret = -XENOMAI_SYSCALL5(sc_cobalt_thread_create, ptid,
				policy, &param_ex, personality, &u_winoff);
	if (ret == 0)
		cobalt_set_tsd(u_winoff);

	/*
	 * We must access anything we'll need from *iargs before
	 * posting the sync semaphore, since our released parent could
	 * unwind the stack space onto which the iargs struct is laid
	 * on before we actually get the CPU back.
	 */
sync_with_creator:
	iargs->ret = ret;
	__STD(sem_post(&iargs->sync));
	if (ret)
		return (void *)ret;

	/*
	 * If the parent thread runs with the same priority as we do,
	 * then we should yield the CPU to it, to preserve the
	 * scheduling order.
	 */
	if (param_ex.sched_priority == parent_prio)
		__STD(sched_yield());

	cobalt_thread_harden();

	retval = start(arg);

	pthread_setmode_np(PTHREAD_WARNSW, 0, NULL);

	return retval;
}

int pthread_create_ex(pthread_t *ptid_r,
		      const pthread_attr_ex_t *attr_ex,
		      void *(*start) (void *), void *arg)
{
	int inherit, detachstate, ret;
	struct pthread_iargs iargs;
	struct sched_param param;
	struct timespec timeout;
	pthread_attr_t attr;
	pthread_t lptid;
	size_t stksz;

	if (attr_ex == NULL)
		attr_ex = &default_attr_ex;

	pthread_getschedparam_ex(pthread_self(), &iargs.policy, &iargs.param_ex);
	iargs.parent_prio = iargs.param_ex.sched_priority;
	memcpy(&attr, &attr_ex->std, sizeof(attr));

	pthread_attr_getinheritsched(&attr, &inherit);
	if (inherit == PTHREAD_EXPLICIT_SCHED) {
		pthread_attr_getschedpolicy_ex(attr_ex, &iargs.policy);
		pthread_attr_getschedparam_ex(attr_ex, &iargs.param_ex);
	}

	if (linuxthreads && geteuid()) {
		/*
		 * Work around linuxthreads shortcoming: it doesn't
		 * believe that it could have RT power as non-root and
		 * fails the thread creation overeagerly.
		 */
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		param.sched_priority = 0;
		pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
		pthread_attr_setschedparam(&attr, &param);
	} else
		/*
		 * Get the created thread to temporarily inherit the
		 * caller priority (we mean linux/libc priority here,
		 * as we use a libc call to create the thread).
		 */
		pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);

	pthread_attr_getdetachstate(&attr, &detachstate);
	pthread_attr_getstacksize(&attr, &stksz);
	pthread_attr_setstacksize(&attr, cobalt_get_stacksize(stksz));
	pthread_attr_getpersonality_ex(attr_ex, &iargs.personality);

	/*
	 * First start a native POSIX thread, then mate a Xenomai
	 * shadow to it.
	 */
	iargs.start = start;
	iargs.arg = arg;
	iargs.ret = EAGAIN;
	__STD(sem_init(&iargs.sync, 0, 0));

	ret = __STD(pthread_create(&lptid, &attr, cobalt_thread_trampoline, &iargs));
	if (ret)
		goto out;

	__STD(clock_gettime(CLOCK_REALTIME, &timeout));
	timeout.tv_sec += 5;
	timeout.tv_nsec = 0;

	for (;;) {
		ret = __STD(sem_timedwait(&iargs.sync, &timeout));
		if (ret && errno == EINTR)
			continue;
		if (ret == 0) {
			ret = iargs.ret;
			if (ret == 0)
				*ptid_r = lptid;
			break;
		} else if (errno == ETIMEDOUT) {
			ret = -EAGAIN;
			break;
		}
		ret = -errno;
		panic("regular sem_wait() failed with %s", symerror(ret));
	}

	cobalt_thread_harden(); /* May fail if regular thread. */
out:
	__STD(sem_destroy(&iargs.sync));

	return ret;
}

/**
 * @fn int pthread_create(pthread_t *ptid_r, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
 * @brief Create a new thread
 *
 * This service creates a thread managed by the Xenomai nucleus in
 * dual kernel configuration.
 *
 * The new thread signal mask is inherited from the current thread, if it was
 * also created with pthread_create(), otherwise the new thread signal mask is
 * empty.
 *
 * Other attributes of the new thread depend on the @a attr
 * argument. If @a attr is NULL, default values for these attributes
 * are used.
 *
 * Returning from the @a start routine has the same effect as calling
 * pthread_exit() with the return value.
 *
 * @param ptid_r address where the identifier of the new thread will be stored on
 * success;
 *
 * @param attr thread attributes;
 *
 * @param start thread start routine;
 *
 * @param arg opaque user-supplied argument passed to @a start;
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a attr is invalid;
 * - EAGAIN, insufficient memory exists in the system heap to create a new
 *   thread, increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EINVAL, thread attribute @a inheritsched is set to PTHREAD_INHERIT_SCHED
 *   and the calling thread does not belong to the Cobalt interface;
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_create.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating or shadowing a Xenomai thread for the first time in
 * user-space, Xenomai installs a handler for the SIGSHADOW signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGSHADOW signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGSHADOW after
 * creating or shadowing the first Xenomai thread, you have to
 * explicitly call the function cobalt_sigshadow_handler at the beginning
 * of your signal handler, using its return to know if the signal was
 * in fact an internal signal of Xenomai (in which case it returns 1),
 * or if you should handle the signal (in which case it returns
 * 0). cobalt_sigshadow_handler prototype is:
 *
 * <b>int cobalt_sigshadow_handler(int sig, struct siginfo *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction,
 * using the SA_SIGINFO flag, and pass all the arguments you received
 * to cobalt_sigshadow_handler.
 */
COBALT_IMPL(int, pthread_create, (pthread_t *ptid_r,
				  const pthread_attr_t *attr,
				  void *(*start) (void *), void *arg))
{
	pthread_attr_ex_t attr_ex;
	struct sched_param param;
	int policy;

	if (attr == NULL)
		attr = &default_attr_ex.std;

	memcpy(&attr_ex.std, attr, sizeof(*attr));
	pthread_attr_getschedpolicy(attr, &policy);
	attr_ex.nonstd.sched_policy = policy;
	pthread_attr_getschedparam(attr, &param);
	attr_ex.nonstd.sched_param.sched_priority = param.sched_priority;
	attr_ex.nonstd.personality = 0; /* Default: use Cobalt. */

	return pthread_create_ex(ptid_r, &attr_ex, start, arg);
}

/**
 * Set the mode of the current thread.
 *
 * This service sets the mode of the calling thread. @a clrmask and @a setmask
 * are two bit masks which are respectively cleared and set in the calling
 * thread status. They are a bitwise OR of the following values:
 * - PTHREAD_LOCK_SCHED, when set, locks the scheduler, which prevents the
 *   current thread from being switched out until the scheduler
 *   is unlocked;
 * - PTHREAD_WARNSW, when set, causes the signal SIGDEBUG to be sent to the
 *   current thread, whenever it involontary switches to secondary mode;
 * - PTHREAD_CONFORMING can be passed in @a setmask to switch the
 * current user-space task to its preferred runtime mode. The only
 * meaningful use of this switch is to force a real-time shadow back
 * to primary mode. Any other use leads to a nop.
 * - PTHREAD_DISABLE_LOCKBREAK disallows breaking the scheduler
 * lock. In the default case, a thread which holds the scheduler lock
 * is allowed to drop it temporarily for sleeping. If this mode bit is set,
 * such thread would return with EINTR immediately from any blocking call.
 *
 * PTHREAD_LOCK_SCHED and PTHREAD_DISABLE_LOCKBREAK are valid for any
 * Xenomai thread, other bits are valid for Xenomai user-space threads
 * only.
 *
 * This service is a non-portable extension of the POSIX interface.
 *
 * @param clrmask set of bits to be cleared;
 *
 * @param setmask set of bits to be set.
 *
 * @param mode_r If non-NULL, @a mode_r must be a pointer to a memory
 * location which will be written upon success with the previous set
 * of active mode bits. If NULL, the previous set of active mode bits
 * will not be returned.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, some bit in @a clrmask or @a setmask is invalid.
 *
 * @note Setting @a clrmask and @a setmask to zero leads to a nop,
 * only returning the previous mode if @a mode_r is a valid address.
 */
int pthread_setmode_np(int clrmask, int setmask, int *mode_r)
{
	return -XENOMAI_SYSCALL3(sc_cobalt_thread_setmode,
				 clrmask, setmask, mode_r);
}

/**
 * Set a thread name.
 *
 * This service set to @a name, the name of @a thread. This name is used for
 * displaying information in /proc/xenomai/sched.
 *
 * This service is a non-portable extension of the POSIX interface.
 *
 * @param thread target thread;
 *
 * @param name name of the thread.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 *
 */
COBALT_IMPL(int, pthread_setname_np, (pthread_t thread, const char *name))
{
	return -XENOMAI_SYSCALL2(sc_cobalt_thread_setname, thread, name);
}

/**
 * Send a signal to a thread.
 *
 * This service send the signal @a sig to the Xenomai POSIX skin thread @a
 * thread (created with pthread_create()). If @a sig is zero, this service check
 * for existence of the thread @a thread, but no signal is sent.
 *
 * @param thread thread identifier;
 *
 * @param sig signal number.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a sig is an invalid signal number;
 * - EAGAIN, the maximum number of pending signals has been exceeded;
 * - ESRCH, @a thread is an invalid thread identifier.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_kill.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, pthread_kill, (pthread_t thread, int sig))
{
	int ret;

	ret = -XENOMAI_SYSCALL2(sc_cobalt_thread_kill, thread, sig);
	if (ret == ESRCH)
		return __STD(pthread_kill(thread, sig));

	return ret;
}

/**
 * Wait for termination of a specified thread.
 *
 * If the thread @a thread is running and joinable, this service blocks the
 * calling thread until the thread @a thread terminates or detaches. In this
 * case, the calling context must be a blockable context (i.e. a Xenomai thread
 * without the scheduler locked) or the root thread (i.e. a module initilization
 * or cleanup routine). When @a thread terminates, the calling thread is
 * unblocked and its return value is stored at* the address @a value_ptr.
 *
 * If, on the other hand, the thread @a thread has already finished execution,
 * its return value is stored at the address @a value_ptr and this service
 * returns immediately. In this case, this service may be called from any
 * context.
 *
 * This service is a cancelation point for POSIX skin threads: if the calling
 * thread is canceled while blocked in a call to this service, the cancelation
 * request is honored and @a thread remains joinable.
 *
 * Multiple simultaneous calls to pthread_join() specifying the same running
 * target thread block all the callers until the target thread terminates.
 *
 * @param thread identifier of the thread to wait for;
 *
 * @param retval address where the target thread return value will be stored
 * on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - EDEADLK, attempting to join the calling thread;
 * - EINVAL, @a thread is detached;
 * - EPERM, the caller context is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_join.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, pthread_join, (pthread_t thread, void **retval))
{
	int ret;

	ret = __STD(pthread_join(thread, retval));
	if (ret)
		return ret;

	ret = cobalt_thread_join(thread);

	return ret == -EBUSY ? EINVAL : 0;
}

/** @} */

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_sched Scheduling management
 *
 * Cobalt/POSIX scheduling management services
 * @{
 */

/**
 * Set the scheduling policy and parameters of the specified thread.
 *
 * This service set the scheduling policy of the Xenomai POSIX skin thread @a
 * tid to the value @a  pol, and its scheduling parameters (i.e. its priority)
 * to the value pointed to by @a par.
 *
 * When used in user-space, passing the current thread ID as @a tid argument,
 * this service turns the current thread into a Xenomai POSIX skin thread. If @a
 * tid is neither the identifier of the current thread nor the identifier of a
 * Xenomai POSIX skin thread this service falls back to the regular
 * pthread_setschedparam() service, hereby causing the current thread to switch
 * to secondary mode if it is Xenomai thread.
 *
 * @param thread target thread;
 *
 * @param policy scheduling policy, one of SCHED_FIFO, SCHED_RR,
 * SCHED_SPORADIC, SCHED_TP or SCHED_OTHER;
 *
 * @param param scheduling parameters address.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid;
 * - EINVAL, @a pol or @a par->sched_priority is invalid;
 * - EAGAIN, in user-space, insufficient memory exists in the system heap,
 *   increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EFAULT, in user-space, @a par is an invalid address;
 * - EPERM, in user-space, the calling process does not have superuser
 *   permissions.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_setschedparam.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating or shadowing a Xenomai thread for the first time in
 * user-space, Xenomai installs a handler for the SIGSHADOW signal. If you had
 * installed a handler before that, it will be automatically called by Xenomai
 * for SIGSHADOW signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGSHADOW after creating
 * or shadowing the first Xenomai thread, you have to explicitly call the
 * function xeno_sigwinch_handler at the beginning of your signal handler,
 * using its return to know if the signal was in fact an internal signal of
 * Xenomai (in which case it returns 1), or if you should handle the signal (in
 * which case it returns 0). xeno_sigwinch_handler prototype is:
 *
 * <b>int xeno_sigwinch_handler(int sig, siginfo_t *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction, using the
 * SA_SIGINFO flag, and pass all the arguments you received to
 * xeno_sigwinch_handler.
 *
 */
COBALT_IMPL(int, pthread_setschedparam, (pthread_t thread,
					 int policy, const struct sched_param *param))
{
	/*
	 * XXX: We currently assume that all available policies
	 * supported by the host kernel define a single scheduling
	 * parameter only, i.e. a priority level.
	 */
	struct sched_param_ex param_ex = {
		.sched_priority = param->sched_priority,
	};

	return pthread_setschedparam_ex(thread, policy, &param_ex);
}

/**
 * Set extended scheduling policy of thread
 *
 * This service is an extended version of the regular
 * pthread_setschedparam() service, which supports Xenomai-specific or
 * additional scheduling policies, not available with the host Linux
 * environment.
 *
 * This service set the scheduling policy of the Xenomai thread @a
 * thread to the value @a policy, and its scheduling parameters
 * (e.g. its priority) to the value pointed to by @a param_ex.
 *
 * If @a thread does not match the identifier of a Xenomai thread, this
 * action falls back to the regular pthread_setschedparam() service.
 *
 * @param thread target Cobalt thread;
 *
 * @param policy scheduling policy, one of SCHED_WEAK, SCHED_FIFO,
 * SCHED_COBALT, SCHED_RR, SCHED_SPORADIC, SCHED_TP, SCHED_QUOTA or
 * SCHED_NORMAL;
 *
 * @param param_ex scheduling parameters address. As a special
 * exception, a negative sched_priority value is interpreted as if
 * SCHED_WEAK was given in @a policy, using the absolute value of this
 * parameter as the weak priority level.
 *
 * When CONFIG_XENO_OPT_SCHED_WEAK is enabled, SCHED_WEAK exhibits
 * priority levels in the [0..99] range (inclusive). Otherwise,
 * sched_priority must be zero for the SCHED_WEAK policy.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - EINVAL, @a policy or @a param_ex->sched_priority is invalid;
 * - EAGAIN, in user-space, insufficient memory exists in the system heap,
 *   increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EFAULT, in user-space, @a param_ex is an invalid address;
 * - EPERM, in user-space, the calling process does not have superuser
 *   permissions.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_setschedparam.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating or shadowing a Xenomai thread for the first time in
 * user-space, Xenomai installs a handler for the SIGSHADOW signal. If
 * you had installed a handler before that, it will be automatically
 * called by Xenomai for SIGSHADOW signals that it has not sent.
 *
 * If, however, you install a signal handler for SIGSHADOW after
 * creating or shadowing the first Xenomai thread, you have to
 * explicitly call the function cobalt_sigshadow_handler at the
 * beginning of your signal handler, using its return to know if the
 * signal was in fact an internal signal of Xenomai (in which case it
 * returns 1), or if you should handle the signal (in which case it
 * returns 0). cobalt_sigshadow_handler prototype is:
 *
 * <b>int cobalt_sigshadow_handler(int sig, struct siginfo *si, void *ctxt);</b>
 *
 * Which means that you should register your handler with sigaction,
 * using the SA_SIGINFO flag, and pass all the arguments you received
 * to cobalt_sigshadow_handler.
 *
 * pthread_setschedparam_ex() may switch the caller to secondary mode.
 */
int pthread_setschedparam_ex(pthread_t thread,
			     int policy, const struct sched_param_ex *param_ex)
{
	int ret, promoted;
	__u32 u_winoff;

	/*
	 * First we tell the libc and the regular kernel about the
	 * policy/param change, then we tell Xenomai.
	 */
	ret = libc_setschedparam(thread, policy, param_ex);
	if (ret)
		return ret;

	ret = -XENOMAI_SYSCALL5(sc_cobalt_thread_setschedparam_ex,
				thread, policy, param_ex,
				&u_winoff, &promoted);

	if (ret == 0 && promoted) {
		commit_stack_memory();
		cobalt_sigshadow_install_once();
		cobalt_set_tsd(u_winoff);
		cobalt_thread_harden();
	}

	return ret;
}

/**
 * Get the scheduling policy and parameters of the specified thread.
 *
 * This service returns, at the addresses @a pol and @a par, the current
 * scheduling policy and scheduling parameters (i.e. priority) of the Xenomai
 * POSIX skin thread @a tid. If this service is called from user-space and @a
 * tid is not the identifier of a Xenomai POSIX skin thread, this service
 * fallback to Linux regular pthread_getschedparam service.
 *
 * @param thread target thread;
 *
 * @param policy address where the scheduling policy of @a tid is stored on
 * success;
 *
 * @param param address where the scheduling parameters of @a tid is stored on
 * success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, pthread_getschedparam, (pthread_t thread,
					 int *__restrict__ policy,
					 struct sched_param *__restrict__ param))
{
	struct sched_param_ex param_ex;
	int ret;

	ret = pthread_getschedparam_ex(thread, policy, &param_ex);
	if (ret)
		return ret;

	param->sched_priority = param_ex.sched_priority;

	return 0;
}

/**
 * Get extended scheduling policy of thread
 *
 * This service is an extended version of the regular
 * pthread_getschedparam() service, which also supports
 * Xenomai-specific or additional POSIX scheduling policies, not
 * available with the host Linux environment.
 *
 * @param thread target thread;
 *
 * @param policy_r address where the scheduling policy of @a thread is stored on
 * success;
 *
 * @param param_ex address where the scheduling parameters of @a thread are
 * stored on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 */
int pthread_getschedparam_ex(pthread_t thread,
			     int *__restrict__ policy_r,
			     struct sched_param_ex *__restrict__ param_ex)
{
	struct sched_param short_param;
	int ret;

	ret = -XENOMAI_SYSCALL3(sc_cobalt_thread_getschedparam_ex,
				thread, policy_r, param_ex);
	if (ret == ESRCH) {
		ret = __STD(pthread_getschedparam(thread, policy_r, &short_param));
		if (ret == 0)
			param_ex->sched_priority = short_param.sched_priority;
	}

	return ret;
}

/**
 * Yield the processor.
 *
 * This function move the current thread at the end of its priority group.
 *
 * @retval 0
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_yield.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, sched_yield, (void))
{
	if (cobalt_get_current() == XN_NO_HANDLE ||
	    (cobalt_get_current_mode() & (XNWEAK|XNRELAX)) == (XNWEAK|XNRELAX))
		return __STD(sched_yield());

	return -XENOMAI_SYSCALL0(sc_cobalt_sched_yield);
}

/**
 * Get minimum priority of the specified scheduling policy.
 *
 * This service returns the minimum priority of the scheduling policy @a
 * policy.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_min.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, sched_get_priority_min, (int policy))
{
	int ret;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		break;
	default:
		ret = XENOMAI_SYSCALL1(sc_cobalt_sched_minprio, policy);
		if (ret >= 0)
			return ret;
		if (ret != -EINVAL) {
			errno = -ret;
			return -1;
		}
	}

	return __STD(sched_get_priority_min(policy));
}

/**
 * Get extended minimum priority of the specified scheduling policy.
 *
 * This service returns the minimum priority of the scheduling policy
 * @a policy, reflecting any Cobalt extension to the standard classes.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_min.html">
 * Specification.</a>
 *
 */
int sched_get_priority_min_ex(int policy)
{
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sched_minprio, policy);
	if (ret >= 0)
		return ret;
	if (ret != -EINVAL) {
		errno = -ret;
		return -1;
	}

	return __STD(sched_get_priority_min(policy));
}

/**
 * Get maximum priority of the specified scheduling policy.
 *
 * This service returns the maximum priority of the scheduling policy @a
 * policy.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_max.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, sched_get_priority_max, (int policy))
{
	int ret;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		break;
	default:
		ret = XENOMAI_SYSCALL1(sc_cobalt_sched_maxprio, policy);
		if (ret >= 0)
			return ret;
		if (ret != -EINVAL) {
			errno = -ret;
			return -1;
		}
	}

	return __STD(sched_get_priority_max(policy));
}

/**
 * Get extended maximum priority of the specified scheduling policy.
 *
 * This service returns the maximum priority of the scheduling policy
 * @a policy, reflecting any Cobalt extension to standard classes.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_max.html">
 * Specification.</a>
 *
 */
int sched_get_priority_max_ex(int policy)
{
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sched_maxprio, policy);
	if (ret >= 0)
		return ret;
	if (ret != -EINVAL) {
		errno = -ret;
		return -1;
	}

	return __STD(sched_get_priority_max(policy));
}

/**
 * Yield the processor.
 *
 * This function move the current thread at the end of its priority group.
 *
 * @retval 0
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_yield.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, pthread_yield, (void))
{
	return __WRAP(sched_yield());
}

/**
 * Set CPU-specific scheduler settings for a policy
 *
 * A configuration is strictly local to the target @a cpu, and may
 * differ from other processors.
 *
 * @param cpu processor to load the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, SCHED_TP and SCHED_QUOTA are valid.
 *
 * @param config a pointer to the configuration data to load on @a
 * cpu, applicable to @a policy.
 *
 * @par Settings applicable to SCHED_TP
 *
 * This call installs the temporal partitions for @a cpu.
 *
 * - config.tp.windows should be a non-null set of time windows,
 * defining the scheduling time slots for @a cpu. Each window defines
 * its offset from the start of the global time frame
 * (windows[].offset), a duration (windows[].duration), and the
 * partition id it applies to (windows[].ptid).
 *
 * Time windows must be strictly contiguous, i.e. windows[n].offset +
 * windows[n].duration shall equal windows[n + 1].offset.
 * If windows[].ptid is in the range
 * [0..CONFIG_XENO_OPT_SCHED_TP_NRPART-1], SCHED_TP threads which
 * belong to the partition being referred to may run for the duration
 * of the time window.
 *
 * Time holes may be defined using windows assigned to the pseudo
 * partition #-1, during which no SCHED_TP threads may be scheduled.
 *
 * - config.tp.nr_windows should define the number of elements present
 * in the config.tp.windows[] array.
 *
 * @a info is ignored for this request.
 *
 * @par Settings applicable to SCHED_QUOTA
 *
 * This call manages thread groups running on @a cpu.
 *
 * - config.quota.op should define the operation to be carried
 * out. Valid operations are:
 *
 *    - sched_quota_add for creating a new thread group on @a cpu.
 *      The new group identifier will be written back to info.tgid
 *      upon success. A new group is given no initial runtime budget
 *      when created. sched_quota_set should be issued to enable it.
 *
 *    - sched_quota_remove for deleting a thread group on @a cpu. The
 *      group identifier should be passed in config.quota.remove.tgid.
 *
 *    - sched_quota_set for updating the scheduling parameters of a
 *      thread group defined on @a cpu. The group identifier should be
 *      passed in config.quota.set.tgid, along with the allotted
 *      percentage of the quota interval (config.quota.set.quota), and
 *      the peak percentage allowed (config.quota.set.quota_peak).
 *
 * All three operations fill in the @a config.info structure with the
 * information reflecting the state of the scheduler on @a cpu with
 * respect to @a policy, after the requested changes have been
 * applied.
 *
 * @param len overall length of the configuration data (in bytes).
 *
 * @return 0 on success;
 * @return an error number if:
 *
 * - EINVAL, @a cpu is invalid, or @a policy is unsupported by the
 * current kernel configuration, @a len is invalid, or @a config
 * contains invalid parameters.
 *
 * - ENOMEM, lack of memory to perform the operation.
 *
 * - EBUSY, with @a policy equal to SCHED_QUOTA, if an attempt is made
 *   to remove a thread group which still manages threads.
 *
 * - ESRCH, with @a policy equal to SCHED_QUOTA, if the group
 *   identifier required to perform the operation is not valid.
 */
int sched_setconfig_np(int cpu, int policy,
		       const union sched_config *config, size_t len)
{
	return -XENOMAI_SYSCALL4(sc_cobalt_sched_setconfig_np,
				 cpu, policy, config, len);
}

/**
 * Retrieve CPU-specific scheduler settings for a policy
 *
 * A configuration is strictly local to the target @a cpu, and may
 * differ from other processors.
 *
 * @param cpu processor to retrieve the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, only SCHED_TP and SCHED_QUOTA are valid input.
 *
 * @param config a pointer to a memory area which receives the
 * configuration settings upon success of this call.
 *
 * @par SCHED_TP specifics
 *
 * On successful return, config->quota.tp contains the TP schedule
 * active on @a cpu.
 *
 * @par SCHED_QUOTA specifics
 *
 * On entry, config->quota.get.tgid must contain the thread group
 * identifier to inquire about.
 *
 * On successful exit, config->quota.info contains the information
 * related to the thread group referenced to by
 * config->quota.get.tgid.
 *
 * @param[in, out] len_r a pointer to a variable for collecting the
 * overall length of the configuration data returned (in bytes). This
 * variable must contain the amount of space available in @a config
 * when the request is issued.
 *
 * @return the number of bytes copied to @a config on success;
 * @return a negative error number if:
 *
 * - EINVAL, @a cpu is invalid, or @a policy is unsupported by the
 * current kernel configuration, or @a len cannot hold the retrieved
 * configuration data.
 *
 * - ESRCH, with @a policy equal to SCHED_QUOTA, if the group
 *   identifier required to perform the operation is not valid
 *   (i.e. config->quota.get.tgid is invalid).
 *
 * - ENOMEM, lack of memory to perform the operation.
 *
 * - ENOSPC, @a len is too short.
 */
ssize_t sched_getconfig_np(int cpu, int policy,
			   union sched_config *config, size_t *len_r)
{
	ssize_t ret;

	ret = XENOMAI_SYSCALL4(sc_cobalt_sched_getconfig_np,
			       cpu, policy, config, *len_r);
	if (ret < 0)
		return -ret;

	*len_r = ret;

	return 0;
}

/** @} */

void cobalt_thread_init(void)
{
#ifdef _CS_GNU_LIBPTHREAD_VERSION
	char vers[128];
	linuxthreads =
		!confstr(_CS_GNU_LIBPTHREAD_VERSION, vers, sizeof(vers))
		|| strstr(vers, "linuxthreads");
#else /* !_CS_GNU_LIBPTHREAD_VERSION */
	linuxthreads = 1;
#endif /* !_CS_GNU_LIBPTHREAD_VERSION */
	pthread_attr_init_ex(&default_attr_ex);
	std_maxpri = __STD(sched_get_priority_max(SCHED_FIFO));
}
