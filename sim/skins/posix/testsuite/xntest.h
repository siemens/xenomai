/*
 * This file is part of the XENOMAI project.
 *
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 *
 * VxWorks is a registered trademark of Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of either the GNU General Public License
 * or the Clarified Artistic License, as specified in the PACKAGE_LICENSE
 * file.
 *
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 */

#ifndef xntest_h
#define xntest_h

#include <nucleus/pod.h>

#ifdef TEST_START
#undef TEST_START
#endif
#define TEST_START(num) (xntest_start())



#ifdef TEST_ASSERT
#undef TEST_ASSERT
#endif
#define TEST_ASSERT(assertion)                                          \
do {                                                                    \
    xnarch_printf(__FILE__ ":%d " #assertion "\n", __LINE__);          \
    xntest_assert((assertion), #assertion, __FILE__, __LINE__ );        \
} while (0)




#ifdef TEST_FINISH
#undef TEST_FINISH
#endif
#define TEST_FINISH() (xntest_finish(__FILE__, __LINE__))



#ifdef TEST_MARK
#undef TEST_MARK
#endif
#define TEST_MARK() (xntest_mark(xnpod_current_thread()))

#ifdef TEST_CHECK_SEQUENCE
#undef TEST_CHECK_SEQUENCE
#endif
#define TEST_CHECK_SEQUENCE xntest_check_seq

#ifdef SEQ
#undef SEQ
#endif
#define SEQ(name, count) 1, name, count

#ifdef END_SEQ
#undef END_SEQ
#endif
#define END_SEQ 0


#ifdef __cplusplus
extern "C" {
#endif

void xntest_start(void);

int xntest_assert(int status, char * assertion, char * file, int line);

void xntest_mark(xnthread_t * thread);

void xntest_check_seq(int next, ...);

void xntest_finish(char * file, int line);

#ifdef __cplusplus
}
#endif


#endif /* !xntest_h */
