#ifndef TIMERFD_H
#define TIMERFD_H

#include <linux/time.h>

int cobalt_timerfd_create(int fd, int clockid, int flags);

int cobalt_timerfd_settime(int fd, int flags,
			const struct itimerspec __user *new_value,
			struct itimerspec __user *old_value);

int cobalt_timerfd_gettime(int fd, struct itimerspec __user *curr_value);

#endif /* TIMERFD_H */
