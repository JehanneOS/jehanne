#include "os.h"
#include <mp.h>
#include "dat.h"

#define VLDIGITS (sizeof(int64_t)/sizeof(mpdigit))

/*
 *  this code assumes that a int64_t is an integral number of
 *  mpdigits int32_t.
 */
mpint*
vtomp(int64_t v, mpint *b)
{
	int s;
	uint64_t uv;

	if(b == nil){
		b = mpnew(VLDIGITS*sizeof(mpdigit));
		jehanne_setmalloctag(b, jehanne_getcallerpc());
	}else
		mpbits(b, VLDIGITS*sizeof(mpdigit));
	b->sign = (v >> (sizeof(v)*8 - 1)) | 1;
	uv = v * b->sign;
	for(s = 0; s < VLDIGITS; s++){
		b->p[s] = uv;
		uv >>= sizeof(mpdigit)*8;
	}
	b->top = s;
	return mpnorm(b);
}

int64_t
mptov(mpint *b)
{
	uint64_t v;
	int s;

	if(b->top == 0)
		return 0LL;

	if(b->top > VLDIGITS){
		if(b->sign > 0)
			return (int64_t)MAXVLONG;
		else
			return (int64_t)MINVLONG;
	}

	v = 0ULL;
	for(s = 0; s < b->top; s++)
		v |= b->p[s]<<(s*sizeof(mpdigit)*8);

	if(b->sign > 0){
		if(v > MAXVLONG)
			v = MAXVLONG;
	} else {
		if(v > MINVLONG)
			v = MINVLONG;
		else
			v = -(int64_t)v;
	}

	return (int64_t)v;
}
