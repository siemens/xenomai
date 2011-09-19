#ifndef __KERNEL__

#include_next <stdlib.h>

#ifndef STDLIB_H
#define STDLIB_H

#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

COBALT_DECL(void, free(void *ptr));

COBALT_DECL(void *, malloc(size_t size));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* STDLIB_H */

#endif /* !__KERNEL__ */
