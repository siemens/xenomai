#ifndef TIMERFD_H
#define TIMERFD_H

#include <linux/time.h>
#include <xenomai/posix/syscall.h>

COBALT_SYSCALL_DECL(timerfd_create,
		    int, (int fd, int clockid, int flags));

COBALT_SYSCALL_DECL(timerfd_settime,
		    int, (int fd, int flags,
			  const struct itimerspec __user *new_value,
			  struct itimerspec __user *old_value));

COBALT_SYSCALL_DECL(timerfd_gettime,
		    int, (int fd, struct itimerspec __user *curr_value));

#endif /* TIMERFD_H */
