#include <u.h>
#include <lib9.h>
#include <authsrv.h>

extern int form1M2B(char *ap, int n, uint8_t key[32]);

int
convM2PR(char *ap, int n, Passwordreq *f, Ticket *t)
{
	uint8_t *p, buf[MAXPASSREQLEN];
	int m;

	memset(f, 0, sizeof(Passwordreq));
	if(t->form == 0){
		m = 1+2*ANAMELEN+1+SECRETLEN;
		if(n < m)
			return -m;
		memmove(buf, ap, m);
		decrypt(t->key, buf, m);
	} else {
		m = 12+2*ANAMELEN+1+SECRETLEN+16;
		if(n < m)
			return -m;
		memmove(buf, ap, m);
		if(form1M2B((char*)buf, m, t->key) < 0)
			return m;
	}
	p = buf;
	f->num = *p++;
	memmove(f->old, p, ANAMELEN), p += ANAMELEN;
	memmove(f->new, p, ANAMELEN), p += ANAMELEN;
	f->changesecret = *p++;
	memmove(f->secret, p, SECRETLEN);
	f->old[ANAMELEN-1] = 0;
	f->new[ANAMELEN-1] = 0;
	f->secret[SECRETLEN-1] = 0;

	return m;
}
