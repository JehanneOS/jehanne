#include <u.h>
#include <lib9.h>
#include <authsrv.h>

extern int form1M2B(char *ap, int n, uint8_t key[32]);

int
convM2A(char *ap, int n, Authenticator *f, Ticket *t)
{
	uint8_t buf[MAXAUTHENTLEN], *p;
	int m;

	memset(f, 0, sizeof(Authenticator));
	if(t->form == 0){
		m = 1+CHALLEN+4;
		if(n < m)
			return -m;
		memmove(buf, ap, m);
		decrypt(t->key, buf, m);
	} else {
		m = 12+CHALLEN+NONCELEN+16;
		if(n < m)
			return -m;
		memmove(buf, ap, m);
		if(form1M2B((char*)buf, m, t->key) < 0)
			return m;
	}
	p = buf;
	f->num = *p++;
	memmove(f->chal, p, CHALLEN);
	p += CHALLEN;
	if(t->form == 1)
		memmove(f->rand, p, NONCELEN);

	return m;
}
