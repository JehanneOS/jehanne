#include "os.h"
#include <mp.h>
#include "dat.h"

mpint*
mpfield(mpint *N)
{
	Mfield *f;

	if(N == nil || N->flags & (MPfield|MPstatic))
		return N;
	if((f = cnfield(N)) != nil)
		goto Exchange;
	if((f = gmfield(N)) != nil)
		goto Exchange;
	return N;
Exchange:
	jehanne_setmalloctag(f, jehanne_getcallerpc());
	mpfree(N);
	return f;
}
