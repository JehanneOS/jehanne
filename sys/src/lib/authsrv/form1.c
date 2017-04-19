#include <u.h>
#include <lib9.h>
#include <authsrv.h>
#include <libsec.h>

/*
 * new ticket format: the reply protector/type is replaced by a
 * 8 byte signature and a 4 byte counter forming the 12 byte
 * nonce for chacha20/poly1305 encryption. a 16 byte poly1305 
 * authentication tag is appended for message authentication.
 * the counter is needed for the AuthPass message which uses
 * the same key for several messages.
 */

static struct {
	char	num;
	char	sig[8];
} form1sig[] = {
	AuthPass,	"form1 PR",	/* password change request encrypted with ticket key */
	AuthTs,		"form1 Ts",	/* ticket encrypted with server's key */
	AuthTc, 	"form1 Tc",	/* ticket encrypted with client's key */
	AuthAs,		"form1 As",	/* server generated authenticator */
	AuthAc,		"form1 Ac",	/* client generated authenticator */
	AuthTp,		"form1 Tp",	/* ticket encrypted with client's key for password change */
	AuthHr,		"form1 Hr",	/* http reply */
};

int
form1check(char *ap, int n)
{
	if(n < 8)
		return -1;

	for(n=0; n<nelem(form1sig); n++)
		if(memcmp(form1sig[n].sig, ap, 8) == 0)
			return form1sig[n].num;

	return -1;
}

int
form1B2M(char *ap, int n, uint8_t key[32])
{
	static uint32_t counter;
	Chachastate s;
	uint8_t *p;
	int i;

	for(i=nelem(form1sig)-1; i>=0; i--)
		if(form1sig[i].num == *ap)
			break;
	if(i < 0)
		abort();

	p = (uint8_t*)ap + 12;
	memmove(p, ap+1, --n);

	/* nonce[12] = sig[8] | counter[4] */
	memmove(ap, form1sig[i].sig, 8);
	i = counter++;
	ap[8] = i, ap[9] = i>>8, ap[10] = i>>16, ap[11] = i>>24;

	setupChachastate(&s, key, 32, (uint8_t*)ap, 12, 20);
	ccpoly_encrypt(p, n, nil, 0, p+n, &s);
	return 12+16 + n;
}

int
form1M2B(char *ap, int n, uint8_t key[32])
{
	Chachastate s;
	uint8_t *p;
	int num;

	num = form1check(ap, n);
	if(num < 0)
		return -1;
	n -= 12+16;
	if(n <= 0)
		return -1;

	p = (uint8_t*)ap + 12;
	setupChachastate(&s, key, 32, (uint8_t*)ap, 12, 20);
	if(ccpoly_decrypt(p, n, nil, 0, p+n, &s))
		return -1;

	memmove(ap+1, p, n);
	ap[0] = num;
	return n+1;
}
