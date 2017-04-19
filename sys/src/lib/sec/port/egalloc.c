#include "os.h"
#include <mp.h>
#include <libsec.h>

EGpub*
egpuballoc(void)
{
	EGpub *eg;

	eg = jehanne_mallocz(sizeof(*eg), 1);
	if(eg == nil)
		jehanne_sysfatal("egpuballoc");
	return eg;
}

void
egpubfree(EGpub *eg)
{
	if(eg == nil)
		return;
	mpfree(eg->p);
	mpfree(eg->alpha);
	mpfree(eg->key);
	jehanne_free(eg);
}


EGpriv*
egprivalloc(void)
{
	EGpriv *eg;

	eg = jehanne_mallocz(sizeof(*eg), 1);
	if(eg == nil)
		jehanne_sysfatal("egprivalloc");
	return eg;
}

void
egprivfree(EGpriv *eg)
{
	if(eg == nil)
		return;
	mpfree(eg->pub.p);
	mpfree(eg->pub.alpha);
	mpfree(eg->pub.key);
	mpfree(eg->secret);
	jehanne_free(eg);
}

EGsig*
egsigalloc(void)
{
	EGsig *eg;

	eg = jehanne_mallocz(sizeof(*eg), 1);
	if(eg == nil)
		jehanne_sysfatal("egsigalloc");
	return eg;
}

void
egsigfree(EGsig *eg)
{
	if(eg == nil)
		return;
	mpfree(eg->r);
	mpfree(eg->s);
	jehanne_free(eg);
}
