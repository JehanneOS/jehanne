#include <u.h>
#include <lib9.h>
#include <authsrv.h>

int
convTR2M(Ticketreq *f, char *ap, int n)
{
	uint8_t *p;

	if(n < TICKREQLEN)
		return 0;

	p = (uint8_t*)ap;
	*p++ = f->type;
	memmove(p, f->authid, ANAMELEN), p += ANAMELEN;
	memmove(p, f->authdom, DOMLEN), p += DOMLEN;
	memmove(p, f->chal, CHALLEN), p += CHALLEN;
	memmove(p, f->hostid, ANAMELEN), p += ANAMELEN;
	memmove(p, f->uid, ANAMELEN), p += ANAMELEN;
	n = p - (uint8_t*)ap;

	return n;
}
