/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include <auth.h>
#include "../boot/boot.h"

static char *pbmsg = "AS protocol botch";
static char *ccmsg = "can't connect to AS";

int32_t
readn(int fd, void *buf, int32_t len)
{
	int m, n;
	char *p;

	p = buf;
	for(n = 0; n < len; n += m){
		m = jehanne_read(fd, p+n, len-n);
		if(m <= 0)
			return -1;
	}
	return n;
}

static char*
fromauth(Method *mp, char *trbuf, char *tbuf)
{
	int afd;
	char t;
	char *msg;
	static char error[2*ERRMAX];

	if(mp->auth == 0)
		fatal("no method for accessing auth server");
	afd = (*mp->auth)();
	if(afd < 0) {
		jehanne_sprint(error, "%s: %r", ccmsg);
		return error;
	}

	if(jehanne_write(afd, trbuf, TICKREQLEN) < 0 || jehanne_read(afd, &t, 1) != 1){
		sys_close(afd);
		jehanne_sprint(error, "%s: %r", pbmsg);
		return error;
	}
	switch(t){
	case AuthOK:
		msg = 0;
		if(jehanne_readn(afd, tbuf, 2*TICKETLEN) < 0) {
			jehanne_sprint(error, "%s: %r", pbmsg);
			msg = error;
		}
		break;
	case AuthErr:
		if(jehanne_readn(afd, error, ERRMAX) < 0) {
			jehanne_sprint(error, "%s: %r", pbmsg);
			msg = error;
		}
		else {
			error[ERRMAX-1] = 0;
			msg = error;
		}
		break;
	default:
		msg = pbmsg;
		break;
	}

	sys_close(afd);
	return msg;
}

void
doauthenticate(int fd, Method *mp)
{
	char *msg;
	char trbuf[TICKREQLEN];
	char tbuf[2*TICKETLEN];

	jehanne_print("session...");
	if(fsession(fd, trbuf, sizeof trbuf) < 0)
		fatal("session command failed");

	/* no authentication required? */
	jehanne_memset(tbuf, 0, 2*TICKETLEN);
	if(trbuf[0] == 0)
		return;

	/* try getting to an auth server */
	jehanne_print("getting ticket...");
	msg = fromauth(mp, trbuf, tbuf);
	jehanne_print("authenticating...");
	if(msg == 0)
		if(sys_fauth(fd, tbuf) >= 0)
			return;

	/* didn't work, go for the security hole */
	jehanne_fprint(2, "no authentication server (%s), using your key as server key\n", msg);
}

char*
checkkey(Method *mp, char *name, char *key)
{
	char *msg;
	Ticketreq tr;
	Ticket t;
	char trbuf[TICKREQLEN];
	char tbuf[TICKETLEN];

	jehanne_memset(&tr, 0, sizeof tr);
	tr.type = AuthTreq;
	jehanne_strcpy(tr.authid, name);
	jehanne_strcpy(tr.hostid, name);
	jehanne_strcpy(tr.uid, name);
	convTR2M(&tr, trbuf);
	msg = fromauth(mp, trbuf, tbuf);
	if(msg == ccmsg){
		jehanne_fprint(2, "boot: can't contact auth server, passwd unchecked\n");
		return 0;
	}
	if(msg)
		return msg;
	convM2T(tbuf, &t, key);
	if(t.num == AuthTc && jehanne_strcmp(name, t.cuid)==0)
		return 0;
	return "no match";
}
