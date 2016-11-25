#include <u.h>
#include <libc.h>
#include <authsrv.h>

int
_asgetticket(int fd, Ticketreq *tr, char *tbuf, int tbuflen)
{
	char err[ERRMAX];
	int i, n, m, r;

	strcpy(err, "AS protocol botch");
	errstr(err, ERRMAX);

	if(_asrequest(fd, tr) < 0)
		return -1;
	if(_asrdresp(fd, tbuf, 0) < 0)
		return -1;

	r = 0;
	for(i = 0; i<2; i++){
		for(n=0; (m = convM2T(tbuf, n, nil, nil)) <= 0; n += m){
			m = -m;
			if(m <= n || m > tbuflen)
				return -1;
			m -= n;
			if(readn(fd, tbuf+n, m) != m)
				return -1;
		}
		r += n;
		tbuf += n;
		tbuflen -= n;
	}

	errstr(err, ERRMAX);

	return r;
}
