#ifndef _XENO_ASM_GENERIC_BITS_MLOCK_ALERT_H
#define _XENO_ASM_GENERIC_BITS_MLOCK_ALERT_H

extern int xeno_sigxcpu_no_mlock;

void xeno_handle_mlock_alert(int sig);

#endif /* _XENO_ASM_GENERIC_BITS_MLOCK_ALERT_H */
