#include <sys/time.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>

#define SAMPLING_PERIOD_US 1000	/* 1Khz sampling period. */

static sem_t semX, semA, semB;

static int sample_count;

static int dim;

static double ref;

int do_histogram = 0, finished = 0;

#define HISTOGRAM_CELLS 1000

unsigned long histogram[HISTOGRAM_CELLS];

static inline void add_histogram (long addval)

{
    long inabs = (addval >= 0 ? addval : -addval); /* 0.1 percent steps */
    histogram[inabs < HISTOGRAM_CELLS ? inabs : HISTOGRAM_CELLS-1]++;
}

static inline void get_time_us (suseconds_t *tp)

{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    *tp = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static inline double compute (void)

{
#define ndims 5000
#define ival  (3.14 * 10000)
    static double a[ndims] = { [ 0 ... ndims - 1 ] = ival },
	          b[ndims] = { [ 0 ... ndims - 1] = ival };
    int j, k;
    double s;

    for (j = 0; j < 1000; j++)
	for (k = dim - 1, s = 0.0; k >= 0; k--)
	    s += a[k] * b[k];

    return s;
}

void dump_histogram (void)

{
    int n;
  
    for (n = 0; n < HISTOGRAM_CELLS; n++)
	{
	long hits = histogram[n];

	if (hits)
	    fprintf(stderr,"%d.%d - %d.%d%%: %ld\n",n / 10,n % 10,(n + 1) / 10,(n + 1) % 10,hits);
	}
}

void *cruncher_thread (void *arg)

{
    struct sched_param param = { .sched_priority = 99 };
    double result;

    if (pthread_setschedparam(pthread_self(),SCHED_FIFO,&param) != 0)
	{
	fprintf(stderr,"pthread_setschedparam() failed\n");
	exit(EXIT_FAILURE);
	}

    for (;;)
	{
	sem_wait(&semA);
        result = compute();

        if (result != ref)
            {
            fprintf(stderr, "Compute returned %f instead of %f, aborting.\n",
                    result, ref);
            exit(EXIT_FAILURE);
            }

	sem_post(&semB);
	}
}

#define IDEAL      10000
#define MARGIN      1000
#define FIRST_DIM    300

void *sampler_thread (void *arg)

{
    struct sched_param param = { .sched_priority = 99 };
    suseconds_t mint1 = 10000000, maxt1 = 0, sumt1 = 0;
    suseconds_t mint2 = 10000000, maxt2 = 0, sumt2 = 0;
    suseconds_t t, t0, t1, ideal;
    int count, pass = 0;
    struct timespec ts;

    dim = FIRST_DIM;
    ref = compute();

    if (pthread_setschedparam(pthread_self(),SCHED_FIFO,&param) != 0)
	{
	fprintf(stderr,"pthread_setschedparam() failed\n");
	exit(EXIT_FAILURE);
	}

    printf("Calibrating cruncher...");

    for(;;) {
        fflush(stdout);
        sleep(1);               /* Let the terminal display the previous
                                   message. */

        get_time_us(&t0);

        for (count = 0; count < 100; count++)
	    {
            sem_post(&semA);
            sem_wait(&semB);
            }

        get_time_us(&t1);

        ideal = (t1 - t0) / count;

        if(++pass > 5 ||
	   dim == ndims ||
	   ((ideal > IDEAL - MARGIN) &&
	    (ideal < IDEAL + MARGIN)))
            break;

        printf("%ld, ", ideal);

        dim = dim*IDEAL/ideal;
        if(dim > ndims)
            dim = ndims;
        ref = compute();
    }

    printf("done -- ideal computation time = %ld us.\n",ideal);

    printf("%d samples, %d hz freq (pid=%d, policy=SCHED_FIFO, prio=99)\n",
	   sample_count,
	   1000000 / SAMPLING_PERIOD_US,
	   getpid());

    sleep(1);

    for (count = 0; count < sample_count; count++)
	{
	/* Wait for SAMPLING_PERIOD_US. */
	ts.tv_sec = 0;
	ts.tv_nsec = SAMPLING_PERIOD_US * 1000;
	get_time_us(&t0);
	nanosleep(&ts,NULL);
	get_time_us(&t1);

	t = t1 - t0;
	if (t > maxt1) maxt1 = t;
	if (t < mint1) mint1 = t;
	sumt1 += t;
	
	/* Run the computational loop. */
	get_time_us(&t0);
	sem_post(&semA);
	sem_wait(&semB);
	get_time_us(&t1);

	t = t1 - t0;
	if (t > maxt2) maxt2 = t;
	if (t < mint2) mint2 = t;
	sumt2 += t;

	if (do_histogram && !finished)
	    add_histogram((t - ideal) * 1000 / ideal);
	}

    printf("--------\nNanosleep jitter: min = %ld us, max = %ld us, avg = %ld us\n",
	   mint1 - SAMPLING_PERIOD_US,
	   maxt1 - SAMPLING_PERIOD_US,
	   (sumt1 / sample_count) - SAMPLING_PERIOD_US);

    printf("Execution jitter: min = %ld us (%ld%%), max = %ld us (%ld%%), avg = %ld us (%ld%%)\n--------\n",
	   mint2 - ideal,
	   (mint2 - ideal) * 100 / ideal,
	   maxt2 - ideal,
	   (maxt2 - ideal) * 100 / ideal,
	   (sumt2 / sample_count) - ideal,
	   ((sumt2 / sample_count) - ideal) * 100 / ideal);

    if (do_histogram)
	dump_histogram();

    __real_sem_post(&semX);

    return NULL;
}

void cleanup_upon_sig(int sig __attribute__((unused)))

{
    finished = 1;

    if (do_histogram)
	dump_histogram();

    exit(0);
}

int main (int ac, char **av)

{
    struct sched_param param = { .sched_priority = 99 };
    pthread_t sampler_thid, cruncher_thid;
    pthread_attr_t thattr;

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);
    signal(SIGHUP, cleanup_upon_sig);

    if (mlockall(MCL_CURRENT|MCL_FUTURE))
	{
	perror("mlockall");
	exit(1);
	}

    if (ac > 1)
	{
	/* ./cruncher --h[istogram] [sample_count] */

	if (strncmp(av[1],"--h",3) == 0)
	    {
	    do_histogram = 1;

	    if (ac > 2)
		sample_count = atoi(av[2]);
	    }
	else
	    sample_count = atoi(av[1]);
	}

    if (sample_count == 0)
	sample_count = 1000;

    sem_init(&semA,0,0);
    sem_init(&semB,0,0);
    __real_sem_init(&semX,0,0);	/* We need a real glibc sema4 here. */

    pthread_attr_init(&thattr);
    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setinheritsched(&thattr,PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&thattr,SCHED_FIFO);
    pthread_attr_setschedparam(&thattr,&param);
    pthread_create(&cruncher_thid,&thattr,&cruncher_thread,NULL);
    pthread_create(&sampler_thid,&thattr,&sampler_thread,NULL);

    __real_sem_wait(&semX);

    return 0;
}
