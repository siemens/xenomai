#ifndef SYSLOG_H
#define SYSLOG_H

#include <stdarg.h>
#include_next <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void __real_syslog(int priority, const char *fmt, ...);

void __real_vsyslog(int priority, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SYSLOG_H */
