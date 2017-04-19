#include "os.h"
#include <mp.h>
#include "dat.h"

mpint*
mprand(int bits, void (*gen)(uint8_t*, int), mpint *b)
{
	mpdigit mask;
	int n, m;
	uint8_t *p;

	n = DIGITS(bits);
	if(b == nil){
		b = mpnew(bits);
		jehanne_setmalloctag(b, jehanne_getcallerpc());
	}else
		mpbits(b, bits);

	p = jehanne_malloc(n*Dbytes);
	if(p == nil)
		jehanne_sysfatal("mprand: %r");
	(*gen)(p, n*Dbytes);
	betomp(p, n*Dbytes, b);
	jehanne_free(p);

	// make sure we don't give too many bits
	m = bits%Dbits;
	if(m == 0)
		return b;

	mask = 1;
	mask <<= m;
	mask--;
	b->p[n-1] &= mask;
	return mpnorm(b);
}
