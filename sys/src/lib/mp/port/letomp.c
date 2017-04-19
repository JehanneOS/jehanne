#include "os.h"
#include <mp.h>
#include "dat.h"

// convert a little endian byte array (least significant byte first) to an mpint
mpint*
letomp(uint8_t *s, uint32_t n, mpint *b)
{
	int i=0, m = 0;
	mpdigit x=0;

	if(b == nil){
		b = mpnew(0);
		jehanne_setmalloctag(b, jehanne_getcallerpc());
	}
	mpbits(b, 8*n);
	for(; n > 0; n--){
		x |= ((mpdigit)(*s++)) << i;
		i += 8;
		if(i == Dbits){
			b->p[m++] = x;
			i = 0;
			x = 0;
		}
	}
	if(i > 0)
		b->p[m++] = x;
	b->top = m;
	b->sign = 1;
	return mpnorm(b);
}
