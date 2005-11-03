/**
 * @file
 * Real-Time Driver Model for Xenomai, driver library
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*!
 * @ingroup rtdm
 * @defgroup driverapi Driver Development API
 *
 * This is the lower interface of RTDM provided to device drivers, currently
 * limited to kernel-space. Real-time drivers should only use functions of
 * this interface in order to remain portable.
 */


#include <linux/delay.h>

#include <xenomai/rtdm/rtdm_driver.h>


/*!
 * @ingroup driverapi
 * @defgroup clock Clock Services
 * @{
 */

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Get system time
 *
 * @return The system time in nanoseconds is returned
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
__u64 rtdm_clock_read(void);
#endif /* DOXYGEN_CPP */
/** @} */


/*!
 * @ingroup driverapi
 * @defgroup rtdmtask Task Services
 * @{
 */

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Intialise and start a real-time task
 *
 * @param[in,out] task Task handle
 * @param[in] name Optional task name
 * @param[in] task_proc Procedure to be executed by the task
 * @param[in] arg Custom argument passed to @c task_proc() on entry
 * @param[in] priority Priority of the task, see also
 * @ref taskprio "Task Priority Range"
 * @param[in] period Period in nanosecons of a cyclic task, 0 for non-cyclic
 * mode
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
int rtdm_task_init(rtdm_task_t *task, const char *name,
                   rtdm_task_proc_t task_proc, void *arg,
                   int priority, __u64 period);

/**
 * @brief Destroy a real-time task
 *
 * @param[in,out] task Task handle as returned by rtdm_task_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_task_destroy(rtdm_task_t *task);

/**
 * @brief Adjust real-time task priority
 *
 * @param[in,out] task Task handle as returned by rtdm_task_init()
 * @param[in] priority New priority of the task, see also
 * @ref taskprio "Task Priority Range"
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_task_set_priority(rtdm_task_t *task, int priority);

/**
 * @brief Adjust real-time task period
 *
 * @param[in,out] task Task handle as returned by rtdm_task_init()
 * @param[in] period New period in nanosecons of a cyclic task, 0 for
 * non-cyclic mode
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
int rtdm_task_set_period(rtdm_task_t *task, __u64 period);

/**
 * @brief Wait on next real-time task period
 *
 * @return 0 on success, otherwise:
 *
 * - -EINVAL is returned if calling task is not in periodic mode.
 *
 * - -ETIMEDOUT is returned if a timer overrun occurred, which indicates
 * that a previous release point has been missed by the calling task.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: always, unless a timer overrun occured.
 */
int rtdm_task_wait_period(void);

/**
 * @brief Activate a blocked real-time task
 *
 * @return Non-zero is returned if the task was actually unblocked from a
 * pending wait state, 0 otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
int rtdm_task_unblock(rtdm_task_t *task);

/**
 * @brief Get current real-time task
 *
 * @return Pointer to task handle
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
rtdm_task_t *rtdm_task_current(void);
#endif /* DOXYGEN_CPP */


/**
 * @brief Wait on a real-time task to terminate
 *
 * @param[in,out] task Task handle as returned by rtdm_task_init()
 * @param[in] poll_delay Polling delay in milliseconds
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task (non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_task_join_nrt(rtdm_task_t *task, unsigned int poll_delay)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    while (!xnthread_test_flags(task, XNZOMBIE)) {
        xnlock_put_irqrestore(&nklock, s);

        msleep(poll_delay);

        xnlock_get_irqsave(&nklock, s);
    }

    xnlock_put_irqrestore(&nklock, s);
}


/**
 * @brief Sleep a specified amount of time
 *
 * @param[in] delay Delay in nanoseconds
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: always.
 */
int rtdm_task_sleep(__u64 delay)
{
    xnthread_t  *thread = xnpod_current_thread();


    xnpod_suspend_thread(thread, XNDELAY, xnpod_ns2ticks(delay), NULL);

    return xnthread_test_flags(thread, XNBREAK) ? -EINTR : 0;
}


/**
 * @brief Sleep until a specified absolute time
 *
 * @param[in] wakeup_time Absolute timeout in nanoseconds
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: always, unless the specified time already passed.
 */
int rtdm_task_sleep_until(__u64 wakeup_time)
{
    xnthread_t  *thread = xnpod_current_thread();
    xnsticks_t  delay;
    spl_t       s;
    int         err = 0;


    xnlock_get_irqsave(&nklock, s);

    delay = xnpod_ns2ticks(wakeup_time) - xnpod_get_time();

    if (likely(delay > 0)) {
        xnpod_suspend_thread(thread, XNDELAY, delay, NULL);

        if (xnthread_test_flags(thread, XNBREAK))
            err = -EINTR;
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * @brief Busy-wait a specified amount of time
 *
 * @param[in] delay Delay in nanoseconds
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (but you should rather avoid this...)
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_task_busy_sleep(__u64 delay)
{
    xnticks_t wakeup = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(delay);

    while (xnarch_get_cpu_tsc() < wakeup)
        cpu_relax();
}
/** @} */



/* --- IPC cleanup helper --- */

#define SYNCH_DELETED   XNSYNCH_SPARE0

void _rtdm_synch_flush(xnsynch_t *synch, unsigned long reason)
{
    spl_t s;


    xnlock_get_irqsave(&nklock,s);

    if (reason == XNRMID)
        setbits(synch->status, SYNCH_DELETED);

    if (likely(xnsynch_flush(synch, reason) == XNSYNCH_RESCHED))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}



/*!
 * @ingroup driverapi
 * @defgroup rtdmsync Synchronisation Services
 * @{
 */

/*!
 * @name Timeout Sequence Management
 * @{
 */

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Initialise a timeout sequence
 *
 * This service initialises a timeout sequence handle according to the given
 * timeout value. Timeout sequences allow to maintain a continuous @a timeout
 * across multiple calls of blocking synchronisation services. A typical
 * application scenario is given below.
 *
 * @param[in,out] timeout_seq Timeout sequence handle
 * @param[in] timeout Relative timeout in nanoseconds, 0 for infinite, or any
 * negative value for non-blocking
 *
 * Application Scenario:
 * @code
int device_service_routine(...)
{
    rtdm_toseq_t timeout_seq;
    ...

    rtdm_toseq_init(&timeout_seq, timeout);
    ...
    while (received < requested) {
        ret = rtdm_event_timedwait(&data_available, timeout, &timeout_seq);
        if (ret < 0)    // including -ETIMEDOUT
            break;

        // receive some data
        ...
    }
    ...
}
 * @endcode
 * Using a timeout sequence in such a scenario avoids that the user-provided
 * relative @c timeout is restarted on every call to rtdm_event_timedwait(),
 * potentially causing an overall delay that is larger than specified by
 * @c timeout. Moreover, all functions supporting timeout sequences also
 * interpret special timeout values (infinite and non-blocking),
 * disburdening the driver developer from handling them separately.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_toseq_init(rtdm_toseq_t *timeout_seq, __s64 timeout);
#endif /* DOXYGEN_CPP */
/** @} */

/*!
 * @name Event Services
 * @{
 */

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Initialise an event
 *
 * @param[in,out] event Event handle
 * @param[in] pending Non-zero if event shall be initialised as set, 0 otherwise
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_event_init(rtdm_event_t *event, unsigned long pending);

/**
 * @brief Destroy an event
 *
 * @param[in,out] event Event handle as returned by rtdm_event_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_event_destroy(rtdm_event_t *event);

/**
 * @brief Signal an event occurrence to currently listening waiters
 *
 * This function wakes up all current waiters of the given event, but it does
 * not change the event state. Subsequently callers of rtdm_event_wait() or
 * rtdm_event_wait_until() will therefore be blocked first.
 *
 * @param[in,out] event Event handle as returned by rtdm_event_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_event_pulse(rtdm_event_t *event);

/**
 * @brief Clear event state
 *
 * @param[in,out] event Event handle as returned by rtdm_event_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_event_clear(rtdm_event_t *event);
#endif /* DOXYGEN_CPP */


/**
 * @brief Signal an event occurrence
 *
 * This function sets the given event and wakes up all current waiters. If no
 * waiter is presently registered, the next call to rtdm_event_wait() or
 * rtdm_event_wait_until() will return immediately.
 *
 * @param[in,out] event Event handle as returned by rtdm_event_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_event_signal(rtdm_event_t *event)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    __set_bit(0, &event->pending);
    if (xnsynch_flush(&event->synch_base, 0))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}


/**
 * @brief Wait on event occurrence
 *
 * This is the light-weight version of rtdm_event_timedwait(), implying an
 * infinite timeout.
 *
 * @param[in,out] event Event handle as returned by rtdm_event_init()
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * - -EIDRM is returned if @a event has been destroyed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
int rtdm_event_wait(rtdm_event_t *event)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (testbits(event->synch_base.status, SYNCH_DELETED))
        err = -EIDRM;
    else if (!__test_and_clear_bit(0, &event->pending)) {
        xnthread_t  *thread = xnpod_current_thread();

        xnsynch_sleep_on(&event->synch_base, XN_INFINITE);

        if (!xnthread_test_flags(thread, XNRMID|XNBREAK))
            __clear_bit(0, &event->pending);
        else if (xnthread_test_flags(thread, XNRMID))
            err = -EIDRM;
        else /* XNBREAK */
            err = -EINTR;
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * @brief Wait on event occurrence with timeout
 *
 * This function waits or tests for the occurence of the given event, taking
 * the provided timeout into account. On successful return, the event is
 * reset.
 *
 * @param[in,out] event Event handle as returned by rtdm_event_init()
 * @param[in] timeout Relative timeout in nanoseconds, 0 for infinite, or any
 * negative value for non-blocking (test for event occurrence)
 * @param[in,out] timeout_seq Handle of a timeout sequence as returned by
 * rtdm_toseq_init() or rtdm_toseq_absinit(), or NULL
 *
 * @return 0 on success, otherwise:
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * - -EIDRM is returned if @a event has been destroyed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
int rtdm_event_timedwait(rtdm_event_t *event, __s64 timeout,
                         rtdm_toseq_t *timeout_seq)
{
    xnthread_t  *thread;
    spl_t       s;
    int         err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (unlikely(testbits(event->synch_base.status, SYNCH_DELETED)))
        err = -EIDRM;
    else if (!__test_and_clear_bit(0, &event->pending)) {
        /* non-blocking mode */
        if (unlikely(timeout < 0)) {
            err = -EWOULDBLOCK;
            goto unlock_out;
        }

        /* timeout sequence */
        if (timeout_seq && (timeout > 0)) {
            timeout = *timeout_seq - xnpod_get_time();
            if (unlikely(timeout <= 0)) {
                err = -ETIMEDOUT;
                goto unlock_out;
            }
            xnsynch_sleep_on(&event->synch_base, timeout);
        }
        /* infinite or relative timeout */
        else
            xnsynch_sleep_on(&event->synch_base, xnpod_ns2ticks(timeout));

        thread = xnpod_current_thread();

        if (!xnthread_test_flags(thread, XNTIMEO|XNRMID|XNBREAK))
            __clear_bit(0, &event->pending);
        else if (xnthread_test_flags(thread, XNTIMEO))
            err = -ETIMEDOUT;
        else if (xnthread_test_flags(thread, XNRMID))
            err = -EIDRM;
        else /* XNBREAK */
            err = -EINTR;
    }

 unlock_out:
    xnlock_put_irqrestore(&nklock, s);

    return err;
}
/** @} */



/*!
 * @name Semaphore Services
 * @{
 */

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Initialise a semaphore
 *
 * @param[in,out] sem Semaphore handle
 * @param[in] value Initial value of the semaphore
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_sem_init(rtdm_sem_t *sem, unsigned long value);

/**
 * @brief Destroy a semaphore
 *
 * @param[in,out] sem Semaphore handle as returned by rtdm_sem_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_sem_destroy(rtdm_sem_t *sem);
#endif /* DOXYGEN_CPP */

/**
 * @brief Decrement a semaphore
 *
 * This is the light-weight version of rtdm_sem_timeddown(), implying an
 * infinite timeout.
 *
 * @param[in,out] sem Semaphore handle as returned by rtdm_sem_init()
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * - -EIDRM is returned if @a sem has been destroyed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
int rtdm_sem_down(rtdm_sem_t *sem)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (testbits(sem->synch_base.status, SYNCH_DELETED))
        err = -EIDRM;
    else if (sem->value > 0)
        sem->value--;
    else {
        xnthread_t  *thread = xnpod_current_thread();

        xnsynch_sleep_on(&sem->synch_base, XN_INFINITE);

        if (xnthread_test_flags(thread, XNRMID|XNBREAK)) {
            if (xnthread_test_flags(thread, XNRMID))
                err = -EIDRM;
            else /*  XNBREAK */
                err = -EINTR;
        }
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * @brief Decrement a semaphore with timeout
 *
 * This function tries to decrement the given semphore's value if it is
 * positive on entry. If not, the caller is blocked unless non-blocking
 * operation was selected.
 *
 * @param[in,out] sem Semaphore handle as returned by rtdm_sem_init()
 * @param[in] timeout Relative timeout in nanoseconds, 0 for infinite, or any
 * negative value for non-blocking operation
 * @param[in,out] timeout_seq Handle of a timeout sequence as returned by
 * rtdm_toseq_init() or rtdm_toseq_absinit(), or NULL
 *
 * @return 0 on success, otherwise:
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is negative and the semaphore
 * value is currently not positive.
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * - -EIDRM is returned if @a sem has been destroyed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
int rtdm_sem_timeddown(rtdm_sem_t *sem, __s64 timeout,
                       rtdm_toseq_t *timeout_seq)
{
    xnthread_t  *thread;
    spl_t       s;
    int         err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (testbits(sem->synch_base.status, SYNCH_DELETED))
        err = -EIDRM;
    else if (sem->value > 0)
        sem->value--;
    else if (timeout < 0)   /* non-blocking mode */
        err = -EWOULDBLOCK;
    else {
        /* timeout sequence */
        if (timeout_seq && (timeout > 0)) {
            timeout = *timeout_seq - xnpod_get_time();
            if (unlikely(timeout <= 0)) {
                err = -ETIMEDOUT;
                goto unlock_out;
            }
            xnsynch_sleep_on(&sem->synch_base, timeout);
        }
        /* infinite or relative timeout */
        else
            xnsynch_sleep_on(&sem->synch_base, xnpod_ns2ticks(timeout));

        thread = xnpod_current_thread();

        if (xnthread_test_flags(thread, XNTIMEO|XNRMID|XNBREAK)) {
            if (xnthread_test_flags(thread, XNTIMEO))
                err = -ETIMEDOUT;
            else if (xnthread_test_flags(thread, XNRMID))
                err = -EIDRM;
            else /*  XNBREAK */
                err = -EINTR;
        }
    }

 unlock_out:
    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * @brief Increment a semaphore
 *
 * This function increments the given semphore's value, waking up a potential
 * waiter which was blocked upon rtdm_sem_down().
 *
 * @param[in,out] sem Semaphore handle as returned by rtdm_sem_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_sem_up(rtdm_sem_t *sem)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    if (xnsynch_wakeup_one_sleeper(&sem->synch_base))
        xnpod_schedule();
    else
        sem->value++;

    xnlock_put_irqrestore(&nklock, s);
}
/** @} */



/*!
 * @name Mutex Services
 * @{
 */

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Initialise a mutex
 *
 * This function initalises a basic mutex with priority inversion protection.
 * "Basic", as it does not allow a mutex owner to recursively lock the same
 * mutex again.
 *
 * @param[in,out] mutex Mutex handle
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_mutex_init(rtdm_mutex_t *mutex);

/**
 * @brief Destroy a mutex
 *
 * @param[in,out] mutex Mutex handle as returned by rtdm_mutex_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_mutex_destroy(rtdm_mutex_t *mutex);
#endif /* DOXYGEN_CPP */


/**
 * @brief Request a mutex
 *
 * This is the light-weight version of rtdm_mutex_timedlock(), implying an
 * infinite timeout.
 *
 * @param[in,out] mutex Mutex handle as returned by rtdm_mutex_init()
 *
 * @return 0 on success, otherwise:
 *
 * - -EIDRM is returned if @a mutex has been destroyed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
int rtdm_mutex_lock(rtdm_mutex_t *mutex)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (testbits(mutex->synch_base.status, SYNCH_DELETED))
        err = -EIDRM;
    else
        while (__test_and_set_bit(0, &mutex->locked)) {
            xnsynch_sleep_on(&mutex->synch_base, XN_INFINITE);

            if (xnthread_test_flags(xnpod_current_thread(), XNRMID)) {
                err = -EIDRM;
                break;
            }
        }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * @brief Request a mutex with timeout
 *
 * This function tries to acquire the given mutex. If it is not available, the
 * caller is blocked unless non-blocking operation was selected.
 *
 * @param[in,out] mutex Mutex handle as returned by rtdm_mutex_init()
 * @param[in] timeout Relative timeout in nanoseconds, 0 for infinite, or any
 * negative value for non-blocking operation
 * @param[in,out] timeout_seq Handle of a timeout sequence as returned by
 * rtdm_toseq_init() or rtdm_toseq_absinit(), or NULL
 *
 * @return 0 on success, otherwise:
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is negative and the semaphore
 * value is currently not positive.
 *
 * - -EINTR is returned if calling task has been unblock by a signal or
 * explicitely via rtdm_task_unblock().
 *
 * - -EIDRM is returned if @a mutex has been destroyed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
int rtdm_mutex_timedlock(rtdm_mutex_t *mutex, __s64 timeout,
                         rtdm_toseq_t *timeout_seq)
{
    xnthread_t  *thread;
    spl_t       s;
    int         err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (testbits(mutex->synch_base.status, SYNCH_DELETED))
        err = -EIDRM;
    else
        while (__test_and_set_bit(0, &mutex->locked)) {
            /* non-blocking mode */
            if (timeout < 0) {
                err = -EWOULDBLOCK;
                break;
            }
            /* timeout sequence */
            if (timeout_seq && (timeout > 0)) {
                timeout = *timeout_seq - xnpod_get_time();
                if (unlikely(timeout <= 0)) {
                    err = -ETIMEDOUT;
                    break;
                }
                xnsynch_sleep_on(&mutex->synch_base, timeout);
            }
            /* infinite or relative timeout */
            else
                xnsynch_sleep_on(&mutex->synch_base, xnpod_ns2ticks(timeout));

            thread = xnpod_current_thread();

            if (xnthread_test_flags(thread, XNTIMEO|XNRMID)) {
                if (xnthread_test_flags(thread, XNTIMEO))
                    err = -ETIMEDOUT;
                else /*  XNRMID */
                    err = -EIDRM;
                break;
            }
        }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * @brief Release a mutex
 *
 * This function releases the given mutex, waking up a potential waiter which
 * was blocked upon rtdm_mutex_lock() or rtdm_mutex_timedlock().
 *
 * @param[in,out] mutex Mutex handle as returned by rtdm_mutex_init()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
void rtdm_mutex_unlock(rtdm_mutex_t *mutex)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    __clear_bit(0, &mutex->locked);
    if (likely(xnsynch_wakeup_one_sleeper(&mutex->synch_base)))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}
/** @} */

/** @} Synchronisation services */


#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */

/*!
 * @ingroup driverapi
 * @defgroup rtdmirq Interrupt Management Services
 * @{
 */

/**
 * @brief Register an interrupt handler
 *
 * @param[in,out] irq_handle IRQ handle
 * @param[in] irq_no Line number of the addressed IRQ
 * @param[in] handler Interrupt handler
 * @param[in] flags Currently unused, pass 0
 * @param[in] device_name Optional device name to show up in real-time IRQ
 * lists (not yet implemented)
 * @param[in] arg Pointer to be passed to the interrupt handler on invocation
 *
 * @return 0 on success, otherwise:
 *
 * - -EINVAL is returned if an invalid parameter was passed.
 *
 * - -EBUSY is returned if the specified IRQ line is already in use.
 *
 * @note To receive interrupts on the requested line, you have to call
 * rtdm_irq_enable() after registering the handler.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_irq_request(rtdm_irq_t *irq_handle, unsigned int irq_no,
                     rtdm_irq_handler_t handler, unsigned long flags,
                     const char *device_name, void *arg);

/**
 * @brief Release an interrupt handler
 *
 * @param[in,out] irq_handle IRQ handle as returned by rtdm_irq_request()
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_irq_free(rtdm_irq_t *irq_handle);

/**
 * @brief Enable interrupt line
 *
 * @param[in,out] irq_handle IRQ handle as returned by rtdm_irq_request()
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: possible.
 */
int rtdm_irq_enable(rtdm_irq_t *irq_handle);

/**
 * @brief Disable interrupt line
 *
 * @param[in,out] irq_handle IRQ handle as returned by rtdm_irq_request()
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_irq_disable(rtdm_irq_t *irq_handle);
/** @} */


/*!
 * @ingroup driverapi
 * @defgroup nrtsignal Non-Real-Time Signalling Services
 *
 * These services provide a mechanism to request the execution of a specified
 * handler in non-real-time context. The triggering can safely be performed in
 * real-time context without suffering from unknown delays. The handler
 * execution will be deferred until the next time the real-time subsystem
 * releases the CPU to the non-real-time part.
 * @{
 */

/**
 * @brief Register a non-real-time signal handler
 *
 * @param[in,out] nrt_sig Signal handle
 * @param[in] handler Non-real-time signal handler
 *
 * @return 0 on success, otherwise:
 *
 * - -EAGAIN is returned if no free signal slot is available.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_nrtsig_init(rtdm_nrtsig_t *nrt_sig, rtdm_nrtsig_handler_t handler);

/**
 * @brief Release a non-realtime signal handler
 *
 * @param[in,out] nrt_sig Signal handle
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_nrtsig_destroy(rtdm_nrtsig_t *nrt_sig);

/**
 * Trigger non-real-time signal
 *
 * @param[in,out] nrt_sig Signal handle
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never in real-time context, possible in non-real-time
 * environments.
 */
void rtdm_nrtsig_pend(rtdm_nrtsig_t *nrt_sig);
/** @} */


/*!
 * @ingroup driverapi
 * @defgroup util Utility Services
 * @{
 */

/**
 * Real-time safe message printing on kernel console
 *
 * @param[in] format Format string (conforming standard @c printf())
 * @param ... Arguments referred by @a format
 *
 * @return On success, this service returns the number of characters printed.
 * Otherwise, a negative error code is returned.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (consider the overhead!)
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never in real-time context, possible in non-real-time
 * environments.
 */
void rtdm_printk(const char *format, ...);

/**
 * Allocate memory block in real-time context
 *
 * @param[in] size Requested size of the memory block
 *
 * @return The pointer to the allocated block is returned on success, NULL
 * otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (consider the overhead!)
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void *rtdm_malloc(size_t size);

/**
 * Release real-time memory block
 *
 * @param[in] ptr Pointer to memory block as returned by rtdm_malloc()
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (consider the overhead!)
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_free(void *ptr);

/**
 * Check if read access to user-space memory block is safe
 *
 * @param[in] user_info User information pointer as passed to the invoked
 * device operation handler
 * @param[in] ptr Address of the user-provided memory block
 * @param[in] size Size of the memory block
 *
 * @return Non-zero is return when it is safe to read from the specified
 * memory block, 0 otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_read_user_ok(rtdm_user_info_t *user_info, const void __user *ptr,
                      size_t size);

/**
 * Check if read/write access to user-space memory block is safe
 *
 * @param[in] user_info User information pointer as passed to the invoked
 * device operation handler
 * @param[in] ptr Address of the user-provided memory block
 * @param[in] size Size of the memory block
 *
 * @return Non-zero is return when it is safe to read from or write to the
 * specified memory block, 0 otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_rw_user_ok(rtdm_user_info_t *user_info, const void __user *ptr,
                    size_t size);

/**
 * Copy user-space memory block to specified buffer
 *
 * @param[in] user_info User information pointer as passed to the invoked
 * device operation handler
 * @param[in] dst Destination buffer address
 * @param[in] src Address of the user-space memory block
 * @param[in] size Size of the memory block
 *
 * @return 0 on success, otherwise:
 *
 * - -EFAULT is returned if an invalid memory area was accessed.
 *
 * @note Before invoking this service, verify via rtdm_read_user_ok() that the
 * provided user-space address can securely be accessed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_copy_from_user(rtdm_user_info_t *user_info, void *dst,
                        const void __user *src, size_t size);

/**
 * Copy specified buffer to user-space memory block
 *
 * @param[in] user_info User information pointer as passed to the invoked
 * device operation handler
 * @param[in] dst Address of the user-space memory block
 * @param[in] src Source buffer address
 * @param[in] size Size of the memory block
 *
 * @return 0 on success, otherwise:
 *
 * - -EFAULT is returned if an invalid memory area was accessed.
 *
 * @note Before invoking this service, verify via rtdm_rw_user_ok() that the
 * provided user-space address can securely be accessed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_copy_to_user(rtdm_user_info_t *user_info, void __user *dst,
                      const void *src, size_t size);

/**
 * Copy user-space string to specified buffer
 *
 * @param[in] user_info User information pointer as passed to the invoked
 * device operation handler
 * @param[in] dst Destination buffer address
 * @param[in] src Address of the user-space string
 * @param[in] count Maximum number of bytes to copy, including the trailing
 * '0'
 *
 * @return 0 on success, otherwise:
 *
 * - -EFAULT is returned if an invalid memory area was accessed.
 *
 * @note This services already includes a check of the source address,
 * calling rtdm_read_user_ok() for @a src explicitly is not required.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_strncpy_from_user(rtdm_user_info_t *user_info, char *dst,
                           const char __user *src, size_t count);

/**
 * Test if running in a real-time task
 *
 * @return Non-zero is returned if the caller resides in real-time context, 0
 * otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
int rtdm_in_rt_context(void);

/** @} */

#endif /* DOXYGEN_CPP */


EXPORT_SYMBOL(rtdm_task_join_nrt);
EXPORT_SYMBOL(rtdm_task_sleep);
EXPORT_SYMBOL(rtdm_task_sleep_until);
EXPORT_SYMBOL(rtdm_task_busy_sleep);
EXPORT_SYMBOL(_rtdm_synch_flush);
EXPORT_SYMBOL(rtdm_event_wait);
EXPORT_SYMBOL(rtdm_event_timedwait);
EXPORT_SYMBOL(rtdm_event_signal);
EXPORT_SYMBOL(rtdm_sem_down);
EXPORT_SYMBOL(rtdm_sem_timeddown);
EXPORT_SYMBOL(rtdm_sem_up);
EXPORT_SYMBOL(rtdm_mutex_lock);
EXPORT_SYMBOL(rtdm_mutex_timedlock);
EXPORT_SYMBOL(rtdm_mutex_unlock);
