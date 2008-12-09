#include <string.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>

#define PROC_ACCT  "/proc/xenomai/acct"
#define PROC_PID  "/proc/%d/cmdline"

#define ACCT_FMT   "%u %d %lu %lu %lu %lx %Lu %Lu %Lu %[^\n]"
#define ACCT_NFMT  10

int main(int argc, char *argv[])
{
	char cmdpath[sizeof(PROC_PID) + 32], cmdbuf[BUFSIZ], name[64];
	unsigned long ssw, csw, pf, state, sec;
	unsigned long long account_period,
		exectime_period, exectime_total, v;
	unsigned int cpu, hr, min, msec, usec;
	FILE *acctfp, *cmdfp;
	int pid;

	acctfp = fopen(PROC_ACCT, "r");
	if (acctfp == NULL)
		error(1, errno, "cannot open %s\n", PROC_ACCT);

	printf("%-6s %-17s   %-24s %s\n\n",
	       "PID", "TIME", "THREAD", "CMD");

	while (fscanf(acctfp, ACCT_FMT,
		      &cpu, &pid, &ssw, &csw, &pf, &state,
		      &account_period, &exectime_period,
		      &exectime_total, name) == ACCT_NFMT) {

		snprintf(cmdpath, sizeof(cmdpath), PROC_PID, pid);
		cmdfp = fopen(cmdpath, "r");

		if (cmdfp == NULL ||
		    fgets(cmdbuf, sizeof(cmdbuf), cmdfp) == NULL)
			strcpy(cmdbuf, "-");

		if (cmdfp)
			fclose(cmdfp);

		v = exectime_total;
		sec = v / 1000000000LL;
		v %= 1000000000LL;
		msec = v / 1000000LL;
		v %= 1000000LL;
		usec = v / 1000LL;
		hr = sec / (60 * 60);
		sec %= (60 * 60);
		min = sec / 60;
		sec %= 60;
		printf("%-6d %.3u:%.2u:%.2lu.%.3u,%.3u   %-24s %s\n",
		       pid,
		       hr, min, sec, msec, usec,
		       name, cmdbuf);
	}

	exit(0);
}
