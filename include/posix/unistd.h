#ifndef _XENO_POSIX_UNISTD_H
#define _XENO_POSIX_UNISTD_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/types.h>
#endif /* __KERNEL__ */

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#endif /* __XENO_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

int ftruncate(int fildes, off_t length);

int close(int fildes);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include_next<unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int __real_ftruncate(int fildes, off_t length);

ssize_t __real_read(int fd, void *buf, size_t nbyte);

ssize_t __real_write(int fd, const void *buf, size_t nbyte);

int __real_close(int fildes);

#ifdef __cplusplus
}
#endif

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_POSIX_UNISTD_H */
