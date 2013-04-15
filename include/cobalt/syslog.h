#ifndef SYSLOG_H
#define SYSLOG_H

#include <stdarg.h>
#include_next <syslog.h>
#include <xeno_config.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

COBALT_DECL(void, syslog(int priority, const char *fmt, ...));

COBALT_DECL(void, vsyslog(int priority,
			  const char *fmt, va_list ap));

#ifdef CONFIG_XENO_FORTIFY
COBALT_DECL(void, __vsyslog_chk(int priority, int level,
				const char *fmt, va_list ap));
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SYSLOG_H */
