#ifndef _XENO_POSIX_SYS_IOCTL_H
#define _XENO_POSIX_SYS_IOCTL_H

#if !(defined(__KERNEL__) || defined(__XENO_SIM__))

#include_next <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

int __real_ioctl(int fildes, int request, ...);

#ifdef __cplusplus
}
#endif

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_POSIX_SYS_IOCTL_H */
