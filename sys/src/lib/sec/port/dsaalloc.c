#include "os.h"
#include <mp.h>
#include <libsec.h>

DSApub*
dsapuballoc(void)
{
	DSApub *dsa;

	dsa = jehanne_mallocz(sizeof(*dsa), 1);
	if(dsa == nil)
		jehanne_sysfatal("dsapuballoc");
	return dsa;
}

void
dsapubfree(DSApub *dsa)
{
	if(dsa == nil)
		return;
	mpfree(dsa->p);
	mpfree(dsa->q);
	mpfree(dsa->alpha);
	mpfree(dsa->key);
	jehanne_free(dsa);
}


DSApriv*
dsaprivalloc(void)
{
	DSApriv *dsa;

	dsa = jehanne_mallocz(sizeof(*dsa), 1);
	if(dsa == nil)
		jehanne_sysfatal("dsaprivalloc");
	return dsa;
}

void
dsaprivfree(DSApriv *dsa)
{
	if(dsa == nil)
		return;
	mpfree(dsa->pub.p);
	mpfree(dsa->pub.q);
	mpfree(dsa->pub.alpha);
	mpfree(dsa->pub.key);
	mpfree(dsa->secret);
	jehanne_free(dsa);
}

DSAsig*
dsasigalloc(void)
{
	DSAsig *dsa;

	dsa = jehanne_mallocz(sizeof(*dsa), 1);
	if(dsa == nil)
		jehanne_sysfatal("dsasigalloc");
	return dsa;
}

void
dsasigfree(DSAsig *dsa)
{
	if(dsa == nil)
		return;
	mpfree(dsa->r);
	mpfree(dsa->s);
	jehanne_free(dsa);
}
