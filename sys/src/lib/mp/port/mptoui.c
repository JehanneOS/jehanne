#include "os.h"
#include <mp.h>
#include "dat.h"

/*
 *  this code assumes that mpdigit is at least as
 *  big as an int.
 */

mpint*
uitomp(uint32_t i, mpint *b)
{
	if(b == nil){
		b = mpnew(0);
		jehanne_setmalloctag(b, jehanne_getcallerpc());
	}
	*b->p = i;
	b->top = 1;
	b->sign = 1;
	return mpnorm(b);
}

uint32_t
mptoui(mpint *b)
{
	uint32_t x;

	x = *b->p;
	if(b->sign < 0)
		x = 0;
	else if(b->top > 1 || (sizeof(mpdigit) > sizeof(uint32_t) && x > MAXUINT))
		x =  MAXUINT;
	return x;
}
