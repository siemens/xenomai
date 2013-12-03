/*
 * Copyright (C) 2008-2011 Gilles Chanteperdrix <gch@xenomai.org>
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
