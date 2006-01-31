#ifndef _XENO_POSIX_TIME_H
#define _XENO_POSIX_TIME_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/time.h>
#endif /* __KERNEL__ */

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#define TIMER_ABSTIME 1
#endif /* __XENO_SIM__ */

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include_next <time.h>
/* In case time.h is included for a side effect of an __need* macro, include it
   a second time to get all definitions. */
#include_next <time.h>

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#ifndef CLOCK_MONOTONIC
/* Some archs do not implement this, but Xenomai always does. */
#define CLOCK_MONOTONIC 1
#endif /* CLOCK_MONOTONIC */

#if defined(__KERNEL__) || defined(__XENO_SIM__)

struct sigevent;

struct timespec;

#ifdef __cplusplus
extern "C" {
#endif

int clock_getres(clockid_t clock_id,
		 struct timespec *res);

int clock_gettime(clockid_t clock_id,
		  struct timespec *tp);

int clock_settime(clockid_t clock_id,
		  const struct timespec *tp);

int clock_nanosleep(clockid_t clock_id,
		    int flags,
                    const struct timespec *rqtp,
		    struct timespec *rmtp);

int nanosleep(const struct timespec *rqtp,
              struct timespec *rmtp);

int timer_create(clockid_t clockid,
		 const struct sigevent *__restrict__ evp,
		 timer_t *__restrict__ timerid);

int timer_delete(timer_t timerid);

int timer_settime(timer_t timerid,
		  int flags,
		  const struct itimerspec *__restrict__ value,
		  struct itimerspec *__restrict__ ovalue);

int timer_gettime(timer_t timerid, struct itimerspec *value);

int timer_getoverrun(timer_t timerid);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

#ifdef __cplusplus
extern "C" {
#endif

int __real_clock_getres(clockid_t clock_id,
			struct timespec *tp);

int __real_clock_gettime(clockid_t clock_id,
			 struct timespec *tp);

int __real_clock_settime(clockid_t clock_id,
			 const struct timespec *tp);

int __real_clock_nanosleep(clockid_t clock_id,
			   int flags,
			   const struct timespec *rqtp,
			   struct timespec *rmtp);

int __real_nanosleep(const struct timespec *rqtp,
		     struct timespec *rmtp);

int __real_timer_create (clockid_t clockid,
			 struct sigevent *evp,
			 timer_t *timerid);

int __real_timer_delete (timer_t timerid);

int __real_timer_settime(timer_t timerid,
			 int flags,
			 const struct itimerspec *value,
			 struct itimerspec *ovalue);

int __real_timer_gettime(timer_t timerid,
			 struct itimerspec *value);

int __real_timer_getoverrun(timer_t timerid);

#ifdef __cplusplus
}
#endif

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_POSIX_TIME_H */
