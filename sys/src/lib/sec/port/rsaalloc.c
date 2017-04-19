#include "os.h"
#include <mp.h>
#include <libsec.h>

RSApub*
rsapuballoc(void)
{
	RSApub *rsa;

	rsa = jehanne_mallocz(sizeof(*rsa), 1);
	if(rsa == nil)
		jehanne_sysfatal("rsapuballoc");
	return rsa;
}

void
rsapubfree(RSApub *rsa)
{
	if(rsa == nil)
		return;
	mpfree(rsa->ek);
	mpfree(rsa->n);
	jehanne_free(rsa);
}


RSApriv*
rsaprivalloc(void)
{
	RSApriv *rsa;

	rsa = jehanne_mallocz(sizeof(*rsa), 1);
	if(rsa == nil)
		jehanne_sysfatal("rsaprivalloc");
	return rsa;
}

void
rsaprivfree(RSApriv *rsa)
{
	if(rsa == nil)
		return;
	mpfree(rsa->pub.ek);
	mpfree(rsa->pub.n);
	mpfree(rsa->dk);
	mpfree(rsa->p);
	mpfree(rsa->q);
	mpfree(rsa->kp);
	mpfree(rsa->kq);
	mpfree(rsa->c2);
	jehanne_free(rsa);
}
