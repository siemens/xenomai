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

#ifdef XNARCH_HAVE_NODIV_LLIMD
unsigned long long
do_nodiv_ullimd(unsigned long long ll, unsigned long long frac, unsigned integ)
{
	return rthal_nodiv_ullimd(ll, frac, integ);
}

long long
do_nodiv_llimd(long long ll, unsigned long long frac, unsigned integ)
{
	return rthal_nodiv_llimd(ll, frac, integ);
}
#endif
