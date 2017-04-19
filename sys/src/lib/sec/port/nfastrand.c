#include "os.h"
#include <libsec.h>

#define Maxrand	((1UL<<31)-1)

uint32_t
nfastrand(uint32_t n)
{
	uint32_t m, r;
	
	/*
	 * set m to the maximum multiple of n <= 2^31-1
	 * so we want a random number < m.
	 */
	if(n > Maxrand)
		jehanne_sysfatal("nfastrand: n too large");

	m = Maxrand - Maxrand % n;
	while((r = fastrand()) >= m)
		;
	return r%n;
}
