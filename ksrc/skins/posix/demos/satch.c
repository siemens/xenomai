#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/mman.h>           /* For mlock. */
#include <unistd.h>
#include <pthread.h>
#include <mqueue.h>

#define CONSUMER_TASK_PRI    1
#define CONSUMER_STACK_SIZE  8192

#define PRODUCER_TASK_PRI    2
#define PRODUCER_STACK_SIZE  8192

#define CONSUMER_WAIT 150
#define PRODUCER_TRIG 40

#define MAX_STRING_LEN 40

#define MQ_NAME "/satchmq"

static const char *satch_s_tunes[] = {
    "Surfing With The Alien",
    "Lords of Karma",
    "Banana Mango",
    "Psycho Monkey",
    "Luminous Flesh Giants",
    "Moroccan Sunset",
    "Satch Boogie",
    "Flying In A Blue Dream",
    "Ride",
    "Summer Song",
    "Speed Of Light",
    "Crystal Planet",
    "Raspberry Jam Delta-V",
    "Champagne?",
    "Clouds Race Across The Sky",
    "Engines Of Creation"
};

void normalize(struct timespec *ts)
{
    if (ts->tv_nsec > 1000000000)
        {
        ts->tv_sec += ts->tv_nsec / 1000000000;
        ts->tv_nsec %= 1000000000;
        }

    if (ts->tv_nsec < 0)
        {
        ts->tv_sec -= (-ts->tv_nsec) / 1000000000 + 1;
        ts->tv_nsec = (-ts->tv_nsec % 1000000000) + 1000000000;
        }
}

static timer_t consumer_tm = (timer_t) -1, producer_tm = (timer_t) -1;
static mqd_t consumer_mq = (mqd_t) -1, producer_mq = (mqd_t) -1;
static pthread_t producer_task, consumer_task;

void abort_perror(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

void *consumer (void *cookie)

{
    char buf[MAX_STRING_LEN];
    struct itimerspec its;
    struct mq_attr mattr;
    struct sigevent evt;
    sigset_t blocked;

    mlockall(MCL_CURRENT|MCL_FUTURE);

    mattr.mq_maxmsg = 30;
    mattr.mq_msgsize = MAX_STRING_LEN;
    consumer_mq = mq_open(MQ_NAME, O_CREAT| O_NONBLOCK| O_RDONLY, 0, &mattr);
    if (consumer_mq == (mqd_t) -1)
        abort_perror("mq_open");

    evt.sigev_notify = SIGEV_SIGNAL;
    evt.sigev_signo = SIGALRM;
    evt.sigev_value.sival_ptr = &evt;
    if(timer_create(CLOCK_REALTIME, &evt, &consumer_tm))
        abort_perror("timer_create");

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &blocked, NULL);

    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = CONSUMER_WAIT * 10000000; /* 10 ms */
    normalize(&its.it_value);
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = CONSUMER_WAIT * 10000000;
    normalize(&its.it_interval);

    if(timer_settime(consumer_tm, 0, &its, NULL))
        abort_perror("timer_settime");

    for (;;)
	{
        siginfo_t si;
        while (sigwaitinfo(&blocked, &si) == -1 && errno == EINTR)
            ;

        for (;;)
	    {
            unsigned prio;
            int nchar;

            do 
                {
                nchar = mq_receive(consumer_mq, buf, sizeof(buf), &prio);
                }
            while (nchar == -1 && errno == EINTR);
            
            if (nchar == -1 && errno == EAGAIN)
                break;

            if (nchar == -1)
                abort_perror("mq_receive");

	    printf("Now playing %s...\n",buf);
	    }
	}

    timer_delete(consumer_tm);
    return NULL;
}

void *producer (void *cookie)

{
    struct itimerspec its;
    struct mq_attr mattr;
    struct sigevent evt;
    sigset_t blocked;
    int next_msg = 0;

    mattr.mq_maxmsg = 30;
    mattr.mq_msgsize = MAX_STRING_LEN;
    producer_mq = mq_open(MQ_NAME, O_CREAT| O_WRONLY, 0, &mattr);
    if (producer_mq == (mqd_t) -1)
        abort_perror("mq_open");
    
    evt.sigev_notify = SIGEV_SIGNAL;
    evt.sigev_signo = SIGRTMIN+1;
    evt.sigev_value.sival_ptr = &evt;
    if (timer_create(CLOCK_REALTIME, &evt, &producer_tm))
        abort_perror("timer_create");

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGRTMIN+1);
    pthread_sigmask(SIG_BLOCK, &blocked, NULL);

    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 10000000 * PRODUCER_TRIG;
    normalize(&its.it_value);
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    normalize(&its.it_interval);

    for (;;)
	{
        const char *msg;
        siginfo_t si;
        int nchar;

        if (timer_settime(producer_tm, 0, &its, NULL))
            abort_perror("timer_settime");
        while (sigwaitinfo(&blocked, &si) == -1 && errno == EINTR)
            ;

	msg = satch_s_tunes[next_msg++];
	next_msg %= (sizeof(satch_s_tunes) / sizeof(satch_s_tunes[0]));

        do 
            {
            nchar = mq_send(producer_mq, msg, strlen(msg) + 1, 0);
            }
        while (nchar == -1 && errno == EINTR);

        if (nchar == -1)
            abort_perror("mq_send");
	}

    timer_delete(producer_tm);
    return NULL;
}

int root_thread_init (void)

{
    struct sched_param parm;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, 1);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    pthread_attr_setstacksize(&attr, CONSUMER_STACK_SIZE);
    parm.sched_priority = CONSUMER_TASK_PRI;
    pthread_attr_setschedparam(&attr, &parm);
    pthread_create(&consumer_task, &attr, &consumer, NULL);

    pthread_attr_setstacksize(&attr, PRODUCER_STACK_SIZE);
    parm.sched_priority = PRODUCER_TASK_PRI;
    pthread_create(&producer_task, &attr, &producer, NULL);

    return 0;
}

void root_thread_exit (void)

{
    timer_delete(producer_tm);
    timer_delete(consumer_tm);
    mq_close(producer_mq);
    mq_close(consumer_mq);
    mq_unlink(MQ_NAME);
}

#ifndef __XENO_SIM__

void cleanup_upon_sig(int sig)
{
    root_thread_exit();
    signal(sig, SIG_DFL);
    raise(sig);
}

int main (int ac, char *av[])

{
    signal(SIGINT, &cleanup_upon_sig);
    signal(SIGTERM, &cleanup_upon_sig);
    signal(SIGHUP, &cleanup_upon_sig);
    signal(SIGALRM, &cleanup_upon_sig);

    atexit(&root_thread_exit);
    root_thread_init();
    pause();

    return 0;
}

#endif /* __XENO_SIM__ */
