#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

int
incref(Ref *r)
{
	int x;

	x = ainc(&r->ref);
	if(x <= 0)
		panic("incref pc=%#p", getcallerpc());
	return x;
}

int
decref(Ref *r)
{
	int x;

	x = adec(&r->ref);
	if(x < 0)
		panic("decref pc=%#p", getcallerpc());
	return x;
}
