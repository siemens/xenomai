/*
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <errno.h>
#include <unistd.h>             /* ftruncate, close. */
#include <fcntl.h>              /* open */
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xenomai/posix/syscall.h>

extern int __pse51_muxid;
extern unsigned long __pse51_mainpid;

int __wrap_shm_open(const char *name, int oflag, mode_t mode)
{
    int err, fd;

    fd = open("/dev/rtheap", oflag, mode);

    if (fd == -1)
        return -1;

    err = -XENOMAI_SKINCALL5(__pse51_muxid,
                             __pse51_shm_open,
                             name,
                             oflag,
                             mode,
                             __pse51_mainpid,
                             fd);
    if (!err)
	return fd;

    close(fd);
    errno = err;
    return -1;
}

int __wrap_shm_unlink(const char *name)
{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
                             __pse51_shm_unlink,
                             name);
    if (!err)
	return 0;

    errno = err;
    return -1;
}

int __wrap_ftruncate(int fildes, off_t length)
{
    int err;

    err = -XENOMAI_SKINCALL3(__pse51_muxid,
                             __pse51_ftruncate,
                             __pse51_mainpid,
                             fildes,
                             length);
    if (!err)
	return 0;

    if (err == EBADF)
        return ftruncate(fildes, length);
    
    errno = err;
    return -1;
}

void *__wrap_mmap(void *addr,
                  size_t len,
                  int prot,
                  int flags,
                  int fildes,
                  off_t off)
{
    struct {
        unsigned long kaddr;
        unsigned long len;
        unsigned long ioctl_cookie;
        unsigned long mapsize;
        unsigned long offset;
    } map;
    void *uaddr;
    int err;

    err = -XENOMAI_SKINCALL5(__pse51_muxid,
                             __pse51_mmap_prologue,
                             len,
                             __pse51_mainpid,
                             fildes,
                             off,
                             &map);

    if (err == EBADF)
        return __real_mmap(addr, len, prot, flags, fildes, off);

    if (err)
        goto error;

    err = ioctl(fildes, 0, map.ioctl_cookie);

    if (err)
        goto err_mmap_epilogue;

    uaddr = __real_mmap(NULL, map.mapsize, prot, flags, fildes, off);

    if (uaddr == MAP_FAILED)
        {
err_mmap_epilogue:
        XENOMAI_SKINCALL3(__pse51_muxid,
                          __pse51_mmap_epilogue,
                          __pse51_mainpid,
                          MAP_FAILED,
                          &map);
        return MAP_FAILED;
        }

    /* Forbid access to map.offset first bytes. */
    mprotect(uaddr, map.offset, PROT_NONE);

    uaddr = (char *) uaddr + map.offset;
    err = -XENOMAI_SKINCALL3(__pse51_muxid,
                             __pse51_mmap_epilogue,
                             __pse51_mainpid,
                             (unsigned long) uaddr,
                             &map);

    if (!err)
	return uaddr;

  error:
    errno = err;
    return MAP_FAILED;
}

int __shm_close(int fd)
{
    int err;

    err = XENOMAI_SKINCALL2(__pse51_muxid,
                            __pse51_shm_close,
                            __pse51_mainpid,
                            fd);

    if (!err)
        return __real_close(fd);

    errno = -err;
    return -1;
}

int __wrap_munmap(void *addr, size_t len)
{
    struct {
        unsigned long mapsize;
        unsigned long offset;
    } map;
    int err;

    err = -XENOMAI_SKINCALL4(__pse51_muxid,
                             __pse51_munmap_prologue,
                             __pse51_mainpid,
                             addr,
                             len,
                             &map);

    if (err == EBADF)
        return __real_munmap(addr, len);

    if (__real_munmap((char *) addr - map.offset, map.mapsize))
        return -1;

    err = -XENOMAI_SKINCALL3(__pse51_muxid,
                             __pse51_munmap_epilogue,
                             __pse51_mainpid,
                             addr,
                             len);

    if (!err)
	return 0;

    errno = err;
    return -1;
}
