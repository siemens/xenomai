#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <execinfo.h>
#include <sys/mman.h>
#include <native/task.h>

RT_TASK task;

void task_body (void *cookie)
{
    /* Ask Xenomai to warn us upon switches to secondary mode. */
    rt_task_set_mode(0, T_WARNSW, NULL);

    /* A real-time task always starts in primary mode. */

    for (;;) {
	rt_task_sleep(1000000000);
	/* Running in primary mode... */
	printf("Switched to secondary mode\n");
	/* ...printf() => write(2): we have just switched to secondary
	   mode: SIGDEBUG should have been sent to us by now. */
    }
}

static const char *reason_str[] = {
    [SIGDEBUG_UNDEFINED] = "undefined",
    [SIGDEBUG_MIGRATE_SIGNAL] = "received signal",
    [SIGDEBUG_MIGRATE_SYSCALL] = "invoked syscall",
    [SIGDEBUG_MIGRATE_FAULT] = "triggered fault",
    [SIGDEBUG_MIGRATE_PRIOINV] = "affected by priority inversion",
    [SIGDEBUG_NOMLOCK] = "missing mlockall",
    [SIGDEBUG_WATCHDOG] = "runaway thread",
};

void warn_upon_switch(int sig, siginfo_t *si, void *context)
{
    unsigned int reason = si->si_value.sival_int;
    void *bt[32];
    int nentries;

    printf("\nSIGDEBUG received, reason %d: %s\n", reason,
	   reason <= SIGDEBUG_WATCHDOG ? reason_str[reason] : "<unknown>");
    /* Dump a backtrace of the frame which caused the switch to
       secondary mode: */
    nentries = backtrace(bt,sizeof(bt) / sizeof(bt[0]));
    backtrace_symbols_fd(bt,nentries,fileno(stdout));
}

int main (int argc, char **argv)
{
    struct sigaction sa;
    int err;

    mlockall(MCL_CURRENT | MCL_FUTURE);

    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = warn_upon_switch;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGDEBUG, &sa, NULL);

    err = rt_task_create(&task,"mytask",0,1,T_FPU);

    if (err) {
	fprintf(stderr,"failed to create task, code %d\n",err);
	return 0;
    }

    err = rt_task_start(&task,&task_body,NULL);

    if (err) {
	fprintf(stderr,"failed to start task, code %d\n",err);
	return 0;
    }

    pause();

    return 0;
}
