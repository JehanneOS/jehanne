#include <u.h>
#include <lib9.h>
#include <authsrv.h>

extern int form1check(char *ap, int n);
extern int form1M2B(char *ap, int n, uint8_t key[32]);

int
convM2T(char *ap, int n, Ticket *f, Authkey *k)
{
	uint8_t buf[MAXTICKETLEN], *p;
	int m;

	if(f != nil)
		memset(f, 0, sizeof(Ticket));

	if(n < 8)
		return -8;

	if(form1check(ap, n) < 0){
		m = 1+CHALLEN+2*ANAMELEN+DESKEYLEN;
		if(n < m)
			return -m;
		if(f == nil || k == nil)
			return m;
		f->form = 0;
		memmove(buf, ap, m);
		decrypt(k->des, buf, m);
	} else {
		m = 12+CHALLEN+2*ANAMELEN+NONCELEN+16;
		if(n < m)
			return -m;
		if(f == nil || k == nil)
			return m;
		f->form = 1;
		memmove(buf, ap, m);
		if(form1M2B((char*)buf, m, k->pakkey) < 0)
			return m;
	}
	p = buf;
	f->num = *p++;
	memmove(f->chal, p, CHALLEN), p += CHALLEN;
	memmove(f->cuid, p, ANAMELEN), p += ANAMELEN;
	memmove(f->suid, p, ANAMELEN), p += ANAMELEN;
	memmove(f->key, p, f->form == 0 ? DESKEYLEN : NONCELEN);

	f->cuid[ANAMELEN-1] = 0;
	f->suid[ANAMELEN-1] = 0;

	return m;
}
