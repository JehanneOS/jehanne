#include <u.h>
#include <lib9.h>
#include <authsrv.h>

int
_asrequest(int fd, Ticketreq *tr)
{
	char trbuf[TICKREQLEN];
	int n;

	n = convTR2M(tr, trbuf, sizeof(trbuf));
	if(jehanne_write(fd, trbuf, n) != n)
		return -1;

	return 0;
}
