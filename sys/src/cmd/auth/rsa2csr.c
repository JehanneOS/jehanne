#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

void
usage(void)
{
	fprint(2, "usage: aux/rsa2csr 'C=US ...CN=xxx' [key]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int len;
	uint8_t *cert;
	RSApriv *key;

	fmtinstall('B', mpfmt);
	fmtinstall('H', encodefmt);

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 1 && argc != 2)
		usage();

	if((key = getrsakey(argc-1, argv+1, 1, nil)) == nil)
		sysfatal("%r");

	cert = X509rsareq(key, argv[0], &len);
	if(cert == nil)
		sysfatal("X509rsareq: %r");

	write(1, cert, len);
	exits(0);
}
