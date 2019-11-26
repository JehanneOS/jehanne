#include <u.h>
#include <lib9.h>
#include <authsrv.h>

int
_asgetpakkey(int fd, Ticketreq *tr, Authkey *a)
{
	uint8_t y[PAKYLEN];
	PAKpriv p;
	int type;

	type = tr->type;
	tr->type = AuthPAK;
	if(_asrequest(fd, tr) != 0){
		tr->type = type;
		return -1;
	}
	tr->type = type;
	authpak_new(&p, a, y, 1);
	if(jehanne_write(fd, y, PAKYLEN) != PAKYLEN
	|| _asrdresp(fd, (char*)y, PAKYLEN) != PAKYLEN){
		memset(&p, 0, sizeof(p));
		return -1;
	}
	return authpak_finish(&p, a, y);
}
