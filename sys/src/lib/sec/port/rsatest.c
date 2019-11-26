#include "os.h"
#include <mp.h>
#include <libsec.h>
#include <bio.h>

void
main(void)
{
	int n;
	int64_t start;
	char *p;
	uint8_t buf[4096];
	Biobuf b;
	RSApriv *rsa;
	mpint *clr, *enc, *clr2;

	jehanne_fmtinstall('B', mpfmt);

	rsa = rsagen(1024, 16, 0);
	if(rsa == nil)
		jehanne_sysfatal("rsagen");
	Binit(&b, 0, OREAD);
	clr = mpnew(0);
	clr2 = mpnew(0);
	enc = mpnew(0);

	strtomp("123456789abcdef123456789abcdef123456789abcdef123456789abcdef", nil, 16, clr);
	rsaencrypt(&rsa->pub, clr, enc);
	
	start = jehanne_nsec();
	for(n = 0; n < 10; n++)
		rsadecrypt(rsa, enc, clr);
	jehanne_print("%lld\n", jehanne_nsec()-start);

	start = jehanne_nsec();
	for(n = 0; n < 10; n++)
		mpexp(enc, rsa->dk, rsa->pub.n, clr2);
	jehanne_print("%lld\n", jehanne_nsec()-start);

	if(mpcmp(clr, clr2) != 0)
		jehanne_print("%B != %B\n", clr, clr2);
	
	jehanne_print("> ");
	while(p = Brdline(&b, '\n')){
		n = Blinelen(&b);
		letomp((uint8_t*)p, n, clr);
		jehanne_print("clr %B\n", clr);
		rsaencrypt(&rsa->pub, clr, enc);
		jehanne_print("enc %B\n", enc);
		rsadecrypt(rsa, enc, clr);
		jehanne_print("clr %B\n", clr);
		n = mptole(clr, buf, sizeof(buf), nil);
		jehanne_write(1, buf, n);
		jehanne_print("> ");
	}
}
