#ifndef OUTOFLINE_H
#define OUTOFLINE_H

unsigned long long dummy(void);

long long
do_llimd(long long ull, unsigned m, unsigned d);

long long
do_llmulshft(long long ull, unsigned m, unsigned s);

unsigned long long
do_nodiv_ullimd(unsigned long long ull, unsigned long long frac, unsigned integ);

long long
do_nodiv_llimd(long long ll, unsigned long long frac, unsigned integ);

#endif /* OUTOFLINE_H */
