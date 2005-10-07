#ifndef _TESTSUITE_KLATENCY_H
#define _TESTSUITE_KLATENCY_H

#include <xeno_config.h>
#include <nucleus/types.h>

typedef struct latency_stat {

    int minjitter;
    int maxjitter;
    int avgjitter;
    int overrun;

} latency_stat_t;

#endif /* _TESTSUITE_KLATENCY_H */
