#include <u.h>
#include <lib9.h>
#include <authsrv.h>
#include <libsec.h>

extern int form1B2M(char *ap, int n, uint8_t key[32]);

int
convT2M(Ticket *f, char *ap, int n, Authkey *key)
{
	uint8_t *p;

	if(n < 1+CHALLEN+2*ANAMELEN)
		return 0;

	p = (uint8_t*)ap;
	*p++ = f->num;
	memmove(p, f->chal, CHALLEN), p += CHALLEN;
	memmove(p, f->cuid, ANAMELEN), p += ANAMELEN;
	memmove(p, f->suid, ANAMELEN), p += ANAMELEN;
	switch(f->form){
	case 0:
		if(n < 1+CHALLEN+2*ANAMELEN+DESKEYLEN)
			return 0;

		memmove(p, f->key, DESKEYLEN), p += DESKEYLEN;
		n = p - (uint8_t*)ap;
		encrypt(key->des, ap, n);
		return n;
	case 1:
		if(n < 12+CHALLEN+2*ANAMELEN+NONCELEN+16)
			return 0;

		memmove(p, f->key, NONCELEN), p += NONCELEN;
		return form1B2M(ap, p - (uint8_t*)ap, key->pakkey);
	}

	return 0;
}
