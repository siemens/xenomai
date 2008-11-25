#include <sys/mman.h>
#include <stdio.h>

#include <native/timer.h>

#include <asm/xenomai/arith.h>

#include "arith-noinline.h"

static volatile unsigned nsec_per_sec = 1000000000;
static volatile unsigned sample_freq = 33000000;
static volatile long long arg = 0x3ffffffffffffffULL;
static unsigned threshold = 20000; /* 15 usecs */

#define bench(display, f)						\
	do {								\
		unsigned long long result;				\
		avg = rejected = 0;					\
		for (i = 0; i < 10000; i++) {				\
			unsigned long long start, end;			\
									\
			start = rt_timer_tsc();				\
			result = (f);					\
			end = rt_timer_tsc();				\
									\
			if (end - start < threshold)			\
				avg += (end - start);			\
			else						\
				++rejected;				\
		}							\
		if (rejected < 10000) {					\
			avg = rthal_llimd(avg, 10000, 10000 - rejected); \
			avg = rt_timer_tsc2ns(avg) - calib;		\
			fprintf(stderr, "%s: 0x%016llx: %lld.%03llu ns," \
				" rejected %d/10000\n",			\
				display, result, avg / 10000,		\
				((avg >= 0 ? avg : -avg) % 10000) / 10, \
				rejected);				\
		} else							\
			fprintf(stderr, "%s: rejected 10000/10000\n", display); \
	} while (0)	

int main(void)
{
	unsigned mul, shft, rejected;
	long long avg, calib = 0;
#ifdef XNARCH_WANT_NODIV_MULDIV
	rthal_u32frac_t frac;
#endif
	int i;

	/* Prepare. */
	xnarch_init_llmulshft(nsec_per_sec, sample_freq, &mul, &shft);
	fprintf(stderr, "mul: 0x%08x, shft: %d\n", mul, shft);
#ifdef XNARCH_WANT_NODIV_MULDIV
	xnarch_init_u32frac(&frac, nsec_per_sec, sample_freq);
	fprintf(stderr, "integ: %d, frac: 0x%08llx\n", frac.integ, frac.frac);
#endif /* XNARCH_WANT_NODIV_MULDIV */
	threshold = rt_timer_ns2tsc(threshold);

	bench("inline calibration", 0);
	calib = avg;
	bench("inlined llimd", rthal_llimd(arg, nsec_per_sec, sample_freq));
	bench("inlined llmulshft", rthal_llmulshft(arg, mul, shft));
#ifdef XNARCH_WANT_NODIV_MULDIV
	bench("inlined nodiv_llimd",
	      rthal_nodiv_llimd(arg, frac.frac, frac.integ));
#endif /* XNARCH_WANT_NODIV_MULDIV */

	calib = 0;
	bench("out of line calibration", dummy());
	calib = avg;
	bench("out of line llimd",
	      do_llimd(arg, nsec_per_sec, sample_freq));
	bench("out of line llmulshft", do_llmulshft(arg, mul, shft));
#ifdef XNARCH_WANT_NODIV_MULDIV
	bench("out of line nodiv_llimd",
	      do_nodiv_llimd(arg, frac.frac, frac.integ));
#endif /* XNARCH_WANT_NODIV_MULDIV */
	return 0;
}
