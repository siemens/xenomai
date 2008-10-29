#include <stdio.h>
#include <stdlib.h>

#include <asm/xenomai/arith.h>

long long dummy(void)
{
	return 0;
}

long long
do_llimd(long long ll, unsigned m, unsigned d)
{
	return rthal_llimd(ll, m, d);
}

long long
do_llmulshft(long long ll, unsigned m, unsigned s)
{
	return rthal_llmulshft(ll, m, s);
}

long long
do_nodiv_llimd(long long ll, unsigned long long frac, unsigned integ)
{
	static unsigned traced;
	long long res = rthal_nodiv_llimd(ll, frac, integ);
	if (!traced) {
		fprintf(stderr, "res: 0x%016llx\n", res);
		traced = 1;
	}
	return res;
}
