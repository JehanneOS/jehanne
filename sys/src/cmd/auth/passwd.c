#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

void
main(int argc, char **argv)
{
	int fd, n, try;
	Ticketreq tr;
	Ticket t;
	Passwordreq pr;
	Authkey key;
	char buf[512];
	char *s, *user;

	user = getuser();

	ARGBEGIN{
	}ARGEND

	private();

	s = nil;
	if(argc > 0){
		user = argv[0];
		s = strchr(user, '@');
		if(s != nil)
			*s++ = 0;
		if(*user == 0)
			user = getuser();
	}

	fd = authdial(nil, s);
	if(fd < 0)
		error("authdial: %r");

	memset(&tr, 0, sizeof(tr));
	strncpy(tr.uid, user, sizeof(tr.uid)-1);
	tr.type = AuthPass;

	/*
	 *  get a password from the user and try to decrypt the
	 *  ticket.  If it doesn't work we've got a bad password,
	 *  give up.
	 */
	memset(&pr, 0, sizeof(pr));
	readln("Plan 9 Password: ", pr.old, sizeof pr.old, 1);
	passtokey(&key, pr.old);

	/*
	 *  negotiate PAK key. we need to retry in case the AS does
	 *  not support the AuthPAK request or when the user has
	 *  not yet setup a new key and the AS made one up.
	 */
	try = 0;
	authpak_hash(&key, tr.uid);
	if(_asgetpakkey(fd, &tr, &key) < 0){
Retry:
		try++;
		close(fd);
		fd = authdial(nil, s);
		if(fd < 0)
			error("authdial: %r");
	}
	/* send ticket request to AS */
	if(_asrequest(fd, &tr) < 0)
		error("%r");
	if(_asgetresp(fd, &t, nil, &key) < 0)
		error("%r");
	if(t.num != AuthTp || strcmp(t.cuid, tr.uid) != 0){
		if(try == 0)
			goto Retry;
		error("bad password");
	}

	/* loop trying new passwords */
	for(;;){
		pr.changesecret = 0;
		*pr.new = 0;
		readln("change Plan 9 Password? (y/n) ", buf, sizeof buf, 0);
		if(*buf == 'y' || *buf == 'Y'){
			readln("Password: ", pr.new, sizeof pr.new, 1);
			readln("Confirm: ", buf, sizeof buf, 1);
			if(strcmp(pr.new, buf)){
				print("!mismatch\n");
				continue;
			}
		}
		readln("change Inferno/POP password? (y/n) ", buf, sizeof buf, 0);
		if(*buf == 'y' || *buf == 'Y'){
			pr.changesecret = 1;
			readln("make it the same as your plan 9 password? (y/n) ",
				buf, sizeof buf, 0);
			if(*buf == 'y' || *buf == 'Y'){
				if(*pr.new == 0)
					strcpy(pr.secret, pr.old);
				else
					strcpy(pr.secret, pr.new);
			} else {
				readln("Secret: ", pr.secret, sizeof pr.secret, 1);
				readln("Confirm: ", buf, sizeof buf, 1);
				if(strcmp(pr.secret, buf)){
					print("!mismatch\n");
					continue;
				}
			}
		}
		pr.num = AuthPass;
		n = convPR2M(&pr, buf, sizeof(buf), &t);
		if(write(fd, buf, n) != n)
			error("AS protocol botch: %r");
		if(_asrdresp(fd, buf, 0) == 0)
			break;
		fprint(2, "passwd: refused: %r\n");
	}
	close(fd);

	exits(0);
}
