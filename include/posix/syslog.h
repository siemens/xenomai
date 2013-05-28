#ifndef SYSLOG_H
#define SYSLOG_H

#pragma GCC system_header

#include <stdarg.h>
#include_next <syslog.h>
#include <xeno_config.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void __real_syslog(int priority, const char *fmt, ...);

void __real_vsyslog(int priority, const char *fmt, va_list ap);

#ifdef CONFIG_XENO_FORTIFY
void __real___vsyslog_chk(int priority, int level, const char *fmt, va_list ap);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SYSLOG_H */
