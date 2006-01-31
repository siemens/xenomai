#ifndef _XENO_POSIX_SYS_MMAN_H
#define _XENO_POSIX_SYS_MMAN_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <asm/mman.h>
#endif /* __KERNEL__ */

#ifdef __XENO_SIM__
#include_next <sys/mman.h>
#include <posix_overrides.h>
#endif /* __XENO_SIM__ */

#define MAP_FAILED ((void *) -1)

#ifdef __cplusplus
extern "C" {
#endif

int shm_open(const char *name, int oflag, mode_t mode); 

int shm_unlink(const char *name);

void *mmap(void *addr, size_t len, int prot, int flags,
        int fildes, off_t off);

int munmap(void *addr, size_t len);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

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

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_POSIX_SYS_MMAN_H */
