#ifndef _XENO_POSIX_SYS_IOCTL_H
#define _XENO_POSIX_SYS_IOCTL_H

#include_next <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

int __real_ioctl(int fildes, int request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _XENO_POSIX_SYS_IOCTL_H */
