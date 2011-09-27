#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <native/task.h>
#include <native/timer.h>

int main (int argc, char **argv)
{
	char *procname, buf[BUFSIZ], dev[BUFSIZ];
	long mayday = 0, start = 0, trash, end;
	unsigned long long stop;
	unsigned char *p;
	char r, w, x, s;
	int d, ret;
	pid_t pid;
	FILE *fp;

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid) {
		/* We are the parent, wait for the child termination */
		int status;

		pid_t termpid = waitpid(pid, &status, 0);
		if (termpid == -1) {
			perror("waitpid");
			exit(EXIT_FAILURE);
		}
		if (termpid != pid) {
			fprintf(stderr, "Unknown child died\n");
			exit(EXIT_FAILURE);
		}

		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));

		if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV) {
			fprintf(stderr, "Testing watchdog...success\n");
			exit(EXIT_SUCCESS);
		}

		fprintf(stderr, "Invalid status: %d\n", status);
		exit(EXIT_FAILURE);
	}

	mlockall(MCL_CURRENT | MCL_FUTURE);

	rt_task_shadow(NULL, "main", 10, 0);

	asprintf(&procname, "/proc/%d/maps", getpid());
	fp = fopen(procname, "r");
	printf("opening %s (%s)\n", procname, fp ? "ok" : "ko");
	if (fp == NULL)
		exit(EXIT_FAILURE);

	while (fgets(buf, sizeof(buf), fp)) {
		ret = sscanf(buf, "%lx-%lx %c%c%c%c %lx %x:%x %d%s\n",
			     &start, &end, &r, &w, &x, &s, &trash, &d, &d, &d, dev);
		if (ret == 11 && r == 'r' && x == 'x'
		    && !strcmp(dev, "/dev/rtheap") && end - start == 4096) {
			printf("mayday page starting at 0x%lx [%s]\n", start, dev);
			mayday = start;
		}

//		7fea9e9e2000-7fea9e9e3000 r-xs 7f35c000 00:0e 19137800                   /dev/ze
	}

	if (mayday) {
		printf("mayday code at %p:", (void *)mayday);
		for (p = (unsigned char *)mayday; p < (unsigned char *)(mayday + 32); p++)
			printf(" %.2x", *p);
		printf("\n");
	}

	printf("Testing watchdog... (this may take 10s if the test fails)\n");

	fflush(stdout);
	stop = rt_timer_tsc() + rt_timer_ns2tsc(8000000000ULL);
	rt_task_sleep(rt_timer_ns2ticks(100000000));

	while (rt_timer_tsc() < stop)
		;

	printf("Testing watchdog...failed\n");
	exit(EXIT_FAILURE);
}
