#ifndef _XENO_POSIX_SYS_MMAN_H
#define _XENO_POSIX_SYS_MMAN_H

#include_next <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif

int __real_shm_open(const char *name, int oflag, mode_t mode); 

int __real_shm_unlink(const char *name);

void *__real_mmap(void *addr,
                  size_t len,
                  int prot,
                  int flags,
                  int fildes,
                  off_t off);

int __real_munmap(void *addr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _XENO_POSIX_SYS_MMAN_H */
