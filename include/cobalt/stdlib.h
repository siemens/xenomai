#ifndef __KERNEL__

#include_next <stdlib.h>

#ifndef STDLIB_H
#define STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void __real_free(void *ptr);

void *__real_malloc(size_t size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* STDLIB_H */

#endif /* !__KERNEL__ */
