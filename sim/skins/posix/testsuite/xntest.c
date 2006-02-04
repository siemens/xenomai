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

#include <nucleus/xenomai.h>
#include "xntest.h"
#include <stdarg.h>

typedef struct xntest_mark
{
    char *threadname;
    int count;
    xnholder_t link;

#define link2mark(laddr)                                                        \
((xntest_mark_t *)(((char *)laddr) - (int)(&((xntest_mark_t *)0)->link)))

} xntest_mark_t;


typedef void (*xntimer_handler) (void *);



static xnqueue_t marks_q;
static xnlock_t test_lock = XNARCH_LOCK_UNLOCKED;
static xntimer_t watchdog;
static int test_failures;
static int tests;



static inline xnholder_t *gettailq (xnqueue_t *qslot) {
    xnholder_t *holder = qslot->head.last;
    if (holder == &qslot->head) return NULL;
    return holder;
}

/* 30 seconds */
#define test_timeout 30000

static inline int strings_differ(const char *str1, const char *str2)
{
    return ((!str1 || !str2) ? str1!=str2 : strcmp(str1, str2));
}

static void interrupt_test (void *dummy)
{
   xnpod_fatal("test interrupted by watchdog.\n");
}



void xntest_start(void)
{
    spl_t s;

    xnlock_get_irqsave(&test_lock, s);
    xntimer_init(&watchdog, interrupt_test, 0);
    xntimer_start(&watchdog, xnpod_ns2ticks(test_timeout * 1000000ULL), XN_INFINITE);

    initq(&marks_q);
    tests=0;
    test_failures=0;
    xnlock_put_irqrestore(&test_lock, s);
}



int xntest_assert(int status, char *assertion, char *file, int line)
{
    spl_t s;

    xnlock_get_irqsave(&test_lock, s);
    ++tests;
    if(!status) {
        ++test_failures;
        xnarch_printf("%s:%d: TEST %s failed.\n", file, line, assertion);
    } else
        xnarch_printf("%s:%d TEST passed.\n", file, line);
    xnlock_put_irqrestore(&test_lock, s);

    return status;
}

void xntest_mark(xnthread_t *thread)
{
    xnholder_t *holder;
    xntest_mark_t *mark;
    const char *threadname;
    spl_t s;

    xnlock_get_irqsave(&test_lock, s);
    holder = gettailq(&marks_q);
    threadname = xnthread_name(thread);

    if(!holder ||
       strings_differ(threadname, (mark=link2mark(holder))->threadname)) {
        size_t namelen = threadname ? strlen(threadname)+1: 0;
        mark = (xntest_mark_t *) xnmalloc(sizeof(xntest_mark_t)+namelen);
        mark->threadname=(threadname
                          ? (char *) mark + sizeof(xntest_mark_t)
                          : NULL);
        if(mark->threadname)
            memcpy(mark->threadname, threadname, namelen);
        
        mark->count = 1;
        inith(&mark->link);
        appendq(&marks_q, &mark->link);
    } else
        mark->count++;
    xnlock_put_irqrestore(&test_lock, s);
}



void xntest_check_seq(int next, ...)
{
    xntest_mark_t *mark;
    xnholder_t *holder;
    va_list args;
    char *name;
    int count;
    spl_t s;

    va_start(args, next);

    xnlock_get_irqsave(&test_lock, s);
    holder = getheadq(&marks_q);

    while(next) {
        name = va_arg(args,char *);
        count = va_arg(args,int);
        ++tests;
        if(holder == NULL) {
            xnarch_printf("Expected sequence: SEQ(\"%s\",%d); "
                          "reached end of recorded sequence.\n", name, count);
            ++test_failures;
        } else {
            mark = link2mark(holder);

            if(strings_differ(mark->threadname, name) || mark->count != count ) {
                xnarch_printf("Expected sequence: SEQ(\"%s\",%d); "
                              "got SEQ(\"%s\",%d)\n",
                              name, count, mark->threadname, mark->count);
                ++test_failures;
            } else
                xnarch_printf("Correct sequence: SEQ(\"%s\",%d)\n", name, count);

            holder = nextq(&marks_q, holder);
        }
        next = va_arg(args, int);
    }
    xnlock_put_irqrestore(&test_lock, s);
    va_end(args);
}



void xntest_finish(char *file, int line)
{
    xnholder_t *next_holder;
    xnholder_t *holder;
    spl_t s;
    
    xnlock_get_irqsave(&test_lock, s);
    for(holder = getheadq(&marks_q); holder ; holder=next_holder)
    {
        next_holder = nextq(&marks_q, holder);
        removeq(&marks_q, holder);
        xnfree(link2mark(holder));
    }
    xnlock_put_irqrestore(&test_lock, s);

    xnarch_printf("%s:%d, test finished: %d failures/ %d tests\n",
                  file, line, test_failures, tests);
    xnpod_fatal("Normal exit.\n");
}
