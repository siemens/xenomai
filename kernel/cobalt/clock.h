#ifndef CLOCK_H
#define CLOCK_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int cobalt_clock_getres(clockid_t clock_id, struct timespec __user *u_ts);

int cobalt_clock_gettime(clockid_t clock_id, struct timespec __user *u_ts);

int cobalt_clock_settime(clockid_t clock_id, const struct timespec __user *u_ts);

int cobalt_clock_nanosleep(clockid_t clock_id, int flags,
			   const struct timespec __user *u_rqt,
			   struct timespec __user *u_rmt);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CLOCK_H */
