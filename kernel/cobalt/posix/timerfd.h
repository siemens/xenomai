#ifndef TIMERFD_H
#define TIMERFD_H

#include <linux/time.h>
#include <xenomai/posix/syscall.h>

int __cobalt_timerfd_settime(int fd, int flags,
			     const struct itimerspec *new_value,
			     struct itimerspec *old_value);

int __cobalt_timerfd_gettime(int fd,
			     struct itimerspec *value);

COBALT_SYSCALL_DECL(timerfd_create,
		    (int clockid, int flags));

COBALT_SYSCALL_DECL(timerfd_settime,
		    (int fd, int flags,
		     const struct itimerspec __user *new_value,
		     struct itimerspec __user *old_value));

COBALT_SYSCALL_DECL(timerfd_gettime,
		    (int fd, struct itimerspec __user *curr_value));

#endif /* TIMERFD_H */
