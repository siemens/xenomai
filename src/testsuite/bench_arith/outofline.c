#include <stdio.h>
#include <stdlib.h>

#include <asm/xenomai/arith.h>

unsigned long long dummy(void)
{
	return 0;
}

unsigned long long
do_ullimd(unsigned long long ull, unsigned m, unsigned d)
{
	unsigned long long res = __rthal_generic_ullimd(ull, m, d);
	static unsigned printed;
	if (!printed) {
		printed = 1;
		fprintf(stderr, "res: 0x%016llx\n",
			(unsigned long long) res);
	}
	return res;
}

unsigned long long
do_llmulshft(unsigned long long ull, unsigned m, unsigned s)
{
	return rthal_llmulshft(ull, m, s);
}

unsigned long long
do_nodiv_ullimd(unsigned long long ull, unsigned long long frac, unsigned integ)
{
	return rthal_nodiv_ullimd(ull, frac, integ);
}
