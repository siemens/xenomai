#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>

#include <native/timer.h>

#include <asm/xenomai/syscall.h>

#ifndef XNARCH_HAVE_NONPRIV_TSC
#define __xn_rdtsc() rt_timer_tsc()
#endif /* !XNARCH_HAVE_NONPRIV_TSC */

int main(int argc, const char *argv[])
{
	unsigned long long runtime, start, jump, tsc1, tsc2;
	unsigned long long sum, g_sum, one_sec;
	unsigned long long loops, g_loops;
	unsigned dt, min, max, g_min, g_max;
	unsigned long long secs;
	unsigned i;

	g_min = ~0U;
	g_max = 0;
	g_sum = 0;
	g_loops = 0;
	one_sec = rt_timer_ns2tsc(1000000000);
	runtime = __xn_rdtsc();

#ifdef __ARMEL__
	if (argc == 2 && !strcmp(argv[1], "-w")) {
		secs = (rt_timer_tsc2ns(__xn_tscinfo.kinfo.mask + 1ULL) + 999999999) / 1000000000;
		fprintf(stderr, "ARM: counter wrap time: %Lu seconds\n", secs);
		min = (2 * secs + 59) / 60;
		secs = min * 60;
	} else
#endif
		secs = 60;
	min = secs / 60;
	fprintf(stderr, "Checking tsc for %u minute(s)\n", min);

	for(i = 0; i < secs; i++) {
		min = ~0U;
		max = 0;
		sum = 0;
		loops = 0;
		tsc2 = start = __xn_rdtsc();
		do {
			tsc1 = __xn_rdtsc();
			if (tsc1 < tsc2) {
				fprintf(stderr, "%016Lx -> %016Lx\n",
					tsc2, tsc1);
				goto err1;
			}
			tsc2 = __xn_rdtsc();
			if (tsc2 < tsc1) {
				fprintf(stderr, "%016Lx -> %016Lx\n",
					tsc1, tsc2);
				goto err2;
			}

			dt = tsc2 - tsc1;

			if (dt > 80)
				continue;

			if (dt < min)
				min = dt;
			if (dt > max)
				max = dt;
			sum += dt;
			++loops;
		} while (tsc2 - start < one_sec);

		fprintf(stderr, "min: %u, max: %u, avg: %g\n",
			min, max, (double)sum / loops);

		if (min < g_min)
			g_min = min;
		if (max > g_max)
			g_max = max;
		g_sum += sum;
		g_loops += loops;
	}

	fprintf(stderr, "min: %u, max: %u, avg: %g -> %g us\n",
		g_min, g_max, (double)g_sum / g_loops,
		(double)rt_timer_tsc2ns(g_sum) / (1000 * g_loops));
	return EXIT_SUCCESS;

  err1:
	runtime = tsc2 - runtime;
	jump = tsc2 - tsc1;
	goto display;
  err2:
	runtime = tsc1 - runtime;
	jump = tsc1 - tsc2;

  display:
	fprintf(stderr, "tsc not monotonic after %Lu ticks, ",
		runtime);
	fprintf(stderr, "jumped back %Lu tick\n", jump);

	return EXIT_FAILURE;

}
