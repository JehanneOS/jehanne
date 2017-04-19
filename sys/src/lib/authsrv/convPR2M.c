#include <u.h>
#include <lib9.h>
#include <authsrv.h>

extern int form1B2M(char *ap, int n, uint8_t key[32]);

int
convPR2M(Passwordreq *f, char *ap, int n, Ticket *t)
{
	uint8_t *p;

	if(n < 1+2*ANAMELEN+1+SECRETLEN)
		return 0;

	p = (uint8_t*)ap;
	*p++ = f->num;
	memmove(p, f->old, ANAMELEN), p += ANAMELEN;
	memmove(p, f->new, ANAMELEN), p += ANAMELEN;
	*p++ = f->changesecret;
	memmove(p, f->secret, SECRETLEN), p += SECRETLEN;
	switch(t->form){
	case 0:
		n = p - (uint8_t*)ap;
		encrypt(t->key, ap, n);
		return n;
	case 1:
		if(n < 12+2*ANAMELEN+1+SECRETLEN+16)
			return 0;
		return form1B2M(ap, p - (uint8_t*)ap, t->key);
	}

	return 0;
}

