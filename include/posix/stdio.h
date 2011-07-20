#ifndef STDIO_H
#define STDIO_H

#ifndef __KERNEL__

#include <stdarg.h>

#include_next <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int __real_vfprintf(FILE *stream, const char *fmt, va_list args);

int __real_vprintf(const char *fmt, va_list args);

int __real_fprintf(FILE *stream, const char *fmt, ...);

int __real_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !__KERNEL__ */

#endif /* STDIO_H */
