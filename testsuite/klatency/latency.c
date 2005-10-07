#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include "latency.h"

#define HISTOGRAM_CELLS 200
int histogram_size = HISTOGRAM_CELLS;
unsigned long *histogram_avg = NULL,
              *histogram_max = NULL,
              *histogram_min = NULL;

int do_histogram = 0, do_stats = 0, finished = 0;
int bucketsize = 1000;		/* bucketsize */
int test_duration = 0;		/* set with -T <sec> */
time_t test_start, test_end;	/* report test duration */
int test_loops = 0;

#define TEN_MILLION    10000000
long gminjitter = TEN_MILLION,
     gmaxjitter = -TEN_MILLION,
     gavgjitter = 0,
     goverrun = 0;

static inline void add_histogram (long *histogram, long addval)
{
    int inabs = (addval >= 0 ? addval : -addval) / bucketsize;
    histogram[inabs < histogram_size ? inabs : histogram_size-1]++;
}

double dump_histogram (long *histogram, char* kind)
{
    int n, total_hits = 0;
    double avg = 0;		/* used to sum hits 1st */

    if (do_histogram)
        fprintf(stderr,"HSH|--param|----range-|--samples\n");

    for (n = 0; n < histogram_size; n++)
      {
	long hits = histogram[n];
	if (hits) {
	    total_hits += hits;
	    avg += n * hits;
	    if (do_histogram)
	        fprintf(stderr,"HSD|    %s| %3d -%3d | %8ld\n",kind, n, n+1, hits);
	}
      }
    avg /= total_hits;	/* compute avg, reuse variable */
    return avg;
}

void dump_stats (long *histogram, char* kind, double avg)
{
    int n, total_hits = 0;
    double variance = 0;

    for (n = 0; n < histogram_size; n++)
      {
	long hits = histogram[n];
	if (hits) {
	    total_hits += hits;
	    variance += hits * (n-avg) * (n-avg);
	}
      }

    /* compute std-deviation (unbiased form) */
    variance /= total_hits - 1;
    variance = sqrt(variance);

    fprintf(stderr,"HSS|    %s| %9d| %10.3f| %10.3f\n",
	    kind, total_hits, avg, variance);
}

void dump_hist_stats (void)
{
    double minavg, maxavg, avgavg;

    /* max is last, where its visible w/o scrolling */
    minavg = dump_histogram (histogram_min, "min");
    avgavg = dump_histogram (histogram_avg, "avg");
    maxavg = dump_histogram (histogram_max, "max");

    fprintf(stderr,"HSH|--param|--samples-|--average--|---stddev--\n");

    dump_stats (histogram_min, "min", minavg);
    dump_stats (histogram_avg, "avg", avgavg);
    dump_stats (histogram_max, "max", maxavg);
}

void cleanup_upon_sig(int sig __attribute__((unused)))
{
    time_t actual_duration;

    if (finished)
	return;

    finished = 1;

    if (do_histogram || do_stats)
	dump_hist_stats();

    time(&test_end);
    actual_duration = test_end - test_start;
    if (!test_duration) test_duration = actual_duration;
    gavgjitter /= (test_loops ?: 2)-1;

    printf("---|------------|------------|------------|--------|-------------------------\n"
	   "RTS|%12ld|%12ld|%12ld|%8ld|    %.2ld:%.2ld:%.2ld/%.2d:%.2d:%.2d\n",
	   gminjitter,
	   gavgjitter,
	   gmaxjitter,
	   goverrun,
	   actual_duration / 3600,(actual_duration / 60) % 60,actual_duration % 60,
	   test_duration / 3600,(test_duration / 60) % 60,test_duration % 60);

    if (histogram_avg)	free(histogram_avg);
    if (histogram_max)	free(histogram_max);
    if (histogram_min)	free(histogram_min);

    sleep(1);
    exit(0);
}

int main (int argc, char **argv)
{
    const char *const communication_channel = "/dev/rtp0";
    int n = 0, c, fd, err, data_lines = 21;
    struct latency_stat s;
    time_t start;
    ssize_t sz;
    int quiet = 0;

    while ((c = getopt(argc,argv,"hl:T:qH:B:s")) != EOF)
	switch (c)
	    {
	    case 'h':

		do_histogram = 1;
		break;
		
	    case 's':

	        do_stats = 1;
		break;

	    case 'H':

		histogram_size = atoi(optarg);
		break;

	    case 'B':

		bucketsize = atoi(optarg);
		break;

	    case 'l':

		data_lines = atoi(optarg);
		break;
		
	    case 'T':

	        test_duration = atoi(optarg);
		alarm(test_duration);
		break;

	    case 'q':

	        quiet = 1;
		break;
		
	    default:
		
		fprintf(stderr, "usage: klatency [options]\n"
			"  [-h]				# print histograms of min, avg, max latencies\n"
			"  [-s]				# print statistics of min, avg, max latencies\n"
			"  [-H <histogram-size>]	# default = 200, increase if your last bucket is full\n"
			"  [-B <bucket-size>]		# default = 1000ns, decrease for more resolution\n"
			"  [-l <data-lines per header>]	# default = 21, 0 supresses header\n"
			"  [-T <seconds_to_test>]	# default = 0, so ^C to end\n"
			"  [-q]				# supresses RTD, RTH lines if -T is used\n");
		exit(2);
	    }

    if (!test_duration && quiet)
        {
	fprintf(stderr, "-q only works if -T is also used\n");
	quiet = 0;
	}

    time(&test_start);

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);
    signal(SIGHUP, cleanup_upon_sig);
    signal(SIGALRM, cleanup_upon_sig);

    setlinebuf(stdout);

    histogram_max = calloc(histogram_size,sizeof(long));
    histogram_min = calloc(histogram_size,sizeof(long));
    histogram_avg = calloc(histogram_size,sizeof(long));

    if (!(histogram_avg && histogram_max && histogram_min))
        cleanup_upon_sig(0);
    
    fd = open(communication_channel, O_RDWR);
    
    if (fd < 0)
        {
        fprintf(stderr, "open(%s): %m\n", communication_channel);
        exit(1);
        }

    time(&start);

    for (;;)
        {
	test_loops++;
        sz = read(fd,&s,sizeof(s));

        if (!sz)
            break;
        
        if (sz != sizeof(s))
            {
            perror("read");
            exit(1);
            }

	if ((do_histogram || do_stats) && !finished)
	  {
	    add_histogram(histogram_max, s.maxjitter);
	    add_histogram(histogram_avg, s.avgjitter);
	    add_histogram(histogram_min, s.minjitter);
	  }

	/* update global jitters */
	if (s.minjitter < gminjitter) gminjitter = s.minjitter;
	if (s.maxjitter > gmaxjitter) gmaxjitter = s.maxjitter;
	gavgjitter += s.avgjitter;
	goverrun = s.overrun;

	if (!quiet)
	    {
	    if (data_lines && (n++ % data_lines)==0)
	      {
		time_t now, dt;
		time(&now);
		dt = now - start;
		printf("RTT|  %.2ld:%.2ld:%.2ld\n",
		       dt / 3600,(dt / 60) % 60,dt % 60);
		printf("RTH|%12s|%12s|%12s|%8s|%12s|%12s\n",
		       "----klat min","----klat avg","----klat max","overrun",
		       "---klat best","--klat worst");
	      }

	    printf("RTD|%12d|%12d|%12d|%8d|%12ld|%12ld\n",
		   s.minjitter,
		   s.avgjitter,
		   s.maxjitter,
		   s.overrun,
		   gminjitter,
		   gmaxjitter);
	    }
	}

    if ((err = close(fd))) {
	perror("close");
	exit(1);
    }
    return 0;
}

