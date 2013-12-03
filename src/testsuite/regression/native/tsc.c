/*
 * Copyright (C) 2011-2012 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

#include <native/timer.h>

#include <asm/xenomai/syscall.h>

#ifndef XNARCH_HAVE_NONPRIV_TSC
#define __xn_rdtsc() rt_timer_tsc()
#endif /* !XNARCH_HAVE_NONPRIV_TSC */

#ifdef HAVE_RECENT_SETAFFINITY
#define do_sched_setaffinity(pid,len,mask) sched_setaffinity(pid,len,mask)
#define do_sched_getaffinity(pid,len,mask) sched_setaffinity(pid,len,mask)
#else /* !HAVE_RECENT_SETAFFINITY */
#ifdef HAVE_OLD_SETAFFINITY
#define do_sched_setaffinity(pid,len,mask) sched_setaffinity(pid,mask)
#define do_sched_getaffinity(pid,len,mask) sched_setaffinity(pid,mask)
#else /* !HAVE_OLD_SETAFFINITY */
#ifndef __cpu_set_t_defined
typedef unsigned long cpu_set_t;
#endif
#define do_sched_setaffinity(pid,len,mask) 0
#define do_sched_getaffinity(pid,len,mask) 0
#ifndef CPU_ZERO
#define	 CPU_ZERO(set)		do { *(set) = 0; } while(0)
#define	 CPU_SET(n,set)	do { *(set) |= (1 << n); } while(0)
#endif
#endif /* HAVE_OLD_SETAFFINITY */
#endif /* HAVE_RECENT_SETAFFINITY */

int main(int argc, const char *argv[])
{
	unsigned long long runtime, start, jump, tsc1, tsc2;
	unsigned long long sum, g_sum, one_sec;
	unsigned long long loops, g_loops;
	unsigned dt, min, max, g_min, g_max;
	unsigned long long secs;
	unsigned i, margin;

#if CONFIG_SMP
	/* Pin the test to the CPU it is currently running on */
	cpu_set_t mask;
	
	if (do_sched_getaffinity(0, sizeof(mask), &mask) == 0)
		for (i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++)
			if (CPU_ISSET(i, &mask)) {
				CPU_ZERO(&mask);
				CPU_SET(i, &mask);
				
				do_sched_setaffinity(0, sizeof(mask), &mask);
				break;
			}
#endif

	g_min = ~0U;
	g_max = 0;
	g_sum = 0;
	g_loops = 0;
	one_sec = rt_timer_ns2tsc(1000000000);
	runtime = __xn_rdtsc();
	margin = rt_timer_ns2tsc(2000);
	if (margin < 80)
		margin = 80;

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

			if (dt > margin)
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
