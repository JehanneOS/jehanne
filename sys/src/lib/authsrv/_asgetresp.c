#include <u.h>
#include <libc.h>
#include <authsrv.h>

int
_asgetresp(int fd, Ticket *t, Authenticator *a, Authkey *k)
{
	char buf[MAXTICKETLEN+MAXAUTHENTLEN], err[ERRMAX];
	int n, m;

	memset(t, 0, sizeof(Ticket));
	if(a != nil)
		memset(a, 0, sizeof(Authenticator));

	strcpy(err, "AS protocol botch");
	errstr(err, ERRMAX);

	if(_asrdresp(fd, buf, 0) < 0)
		return -1;

	for(n = 0; (m = convM2T(buf, n, t, k)) <= 0; n += m){
		m = -m;
		if(m <= n || m > sizeof(buf))
			return -1;
		m -= n;
		if(readn(fd, buf+n, m) != m)
			return -1;
	}

	if(a != nil){
		for(n = 0; (m = convM2A(buf, n, a, t)) <= 0; n += m){
			m = -m;
			if(m <= n || m > sizeof(buf))
				return -1;
			m -= n;
			if(readn(fd, buf+n, m) != m)
				return -1;
		}
	}

	errstr(err, ERRMAX);

	return 0;
}
