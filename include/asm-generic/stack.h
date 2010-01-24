#ifndef STACKSIZE_H
#define STACKSIZE_H

#include <stdint.h>

#include <unistd.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static inline unsigned xeno_stacksize(unsigned size)
{
	static const unsigned default_size = __WORDSIZE * 1024;
	static unsigned min_size;
	if (!min_size)
		min_size = PTHREAD_STACK_MIN + getpagesize();

	if (!size)
		size = default_size;
	if (size < min_size)
		size = min_size;

	return size;
}

void xeno_fault_stack(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* STACKSIZE_H */
