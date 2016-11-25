#include "os.h"
#include <mp.h>
#include "dat.h"

void
mpmod(mpint *x, mpint *n, mpint *r)
{
	int sign;

	sign = x->sign;
	if((n->flags & MPfield) == 0
	|| ((Mfield*)n)->reduce((Mfield*)n, x, r) != 0)
		mpdiv(x, n, nil, r);
	if(sign < 0)
		mpmagsub(n, r, r);
}
