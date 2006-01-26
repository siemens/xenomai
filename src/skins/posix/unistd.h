#ifndef _XENO_POSIX_UNISTD_H
#define _XENO_POSIX_UNISTD_H

#include_next<unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int __real_ftruncate(int fildes, off_t length);

int __real_close(int fildes);

#ifdef __cplusplus
}
#endif

#endif /* _XENO_POSIX_UNISTD_H */
