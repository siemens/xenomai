#ifndef _XENO_POSIX_FCNTL_H
#define _XENO_POSIX_FCNTL_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/fcntl.h>
#endif /* __KERNEL__ */

#ifdef __cplusplus
extern "C" {
#endif

int open(const char *path, int oflag, ...);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include <time.h>
#include_next <fcntl.h>

int __real_open(const char *path, int oflag, ...);

#ifdef __cplusplus
}
#endif

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_POSIX_FCNTL_H */
