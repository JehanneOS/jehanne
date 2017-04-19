#include "os.h"
#include <mp.h>
#include "dat.h"

#define VLDIGITS (sizeof(int64_t)/sizeof(mpdigit))

/*
 *  this code assumes that a int64_t is an integral number of
 *  mpdigits int32_t.
 */
mpint*
uvtomp(uint64_t v, mpint *b)
{
	int s;

	if(b == nil){
		b = mpnew(VLDIGITS*sizeof(mpdigit));
		jehanne_setmalloctag(b, jehanne_getcallerpc());
	}else
		mpbits(b, VLDIGITS*sizeof(mpdigit));
	b->sign = 1;
	for(s = 0; s < VLDIGITS; s++){
		b->p[s] = v;
		v >>= sizeof(mpdigit)*8;
	}
	b->top = s;
	return mpnorm(b);
}

uint64_t
mptouv(mpint *b)
{
	uint64_t v;
	int s;

	if(b->top == 0)
		return 0LL;

	if(b->top > VLDIGITS)
		return MAXVLONG;

	v = 0ULL;
	for(s = 0; s < b->top; s++)
		v |= (uint64_t)b->p[s]<<(s*sizeof(mpdigit)*8);

	return v;
}
