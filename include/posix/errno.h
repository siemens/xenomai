#ifndef _XENO_ERRNO_H
#define _XENO_ERRNO_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/unistd.h>         /* conflicting declaration of errno. */
#endif /* !__KERNEL__ */

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#endif /* __XENO_SIM__ */

/* Error Codes (pse51_errno_location is implemented in sem.c). */
/* errno values pasted from Linux asm/errno.h and bits/errno.h (ENOTSUP). */
#define ENOTSUP         EOPNOTSUPP
#define	ETIMEDOUT	110	/* Connection timed out */

#define errno (*pse51_errno_location())

#ifdef __cplusplus
extern "C" {
#endif

int *pse51_errno_location(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include_next <errno.h>

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_ERRNO_H */
