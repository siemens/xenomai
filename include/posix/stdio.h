#ifndef __KERNEL__

#include_next <stdio.h>

#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int __real_vfprintf(FILE *stream, const char *fmt, va_list args);

int __real_vprintf(const char *fmt, va_list args);

int __real_fprintf(FILE *stream, const char *fmt, ...);

int __real_printf(const char *fmt, ...);

int __real_puts(const char *s);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* STDIO_H */

#endif /* !__KERNEL__ */
