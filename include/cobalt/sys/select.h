#ifndef _XENO_POSIX_SELECT_H
#define _XENO_POSIX_SELECT_H

#ifndef __KERNEL__

#include_next <sys/select.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, select(int __nfds, fd_set *__restrict __readfds,
			fd_set *__restrict __writefds,
			fd_set *__restrict __exceptfds,
			struct timeval *__restrict __timeout));
#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* _XENO_POSIX_SELECT_H */
