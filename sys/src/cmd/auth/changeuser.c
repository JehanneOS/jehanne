/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include <u.h>
#include <lib9.h>
#include <libsec.h>
#include <authsrv.h>
#include <chartypes.h>
#include <bio.h>
#include "authcmdlib.h"

void	install(char*, char*, Authkey*, int32_t, int);
int	exists (char*, char*);

void
usage(void)
{
	fprint(2, "usage: changeuser [-pn] user\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *u, answer[32], p9pass[32];
	int which, newkey, newbio, dosecret;
	int32_t t;
	Authkey key;
	Acctbio a;
	Fs *f;

	fmtinstall('K', deskeyfmt);

	which = 0;
	ARGBEGIN{
	case 'p':
		which |= Plan9;
		break;
	case 'n':
		which |= Securenet;
		break;
	default:
		usage();
	}ARGEND
	argv0 = "changeuser";

	if(argc != 1)
		usage();
	u = *argv;
	if(memchr(u, '\0', ANAMELEN) == 0)
		error("bad user name");

	if(!which)
		which = Plan9;

	newbio = 0;
	t = 0;
	a.user = 0;
	if(which & Plan9){
		f = &fs[Plan9];
		newkey = 1;
		if(exists(f->keys, u)){
			readln("assign new password? [y/n]: ", answer, sizeof answer, 0);
			if(answer[0] != 'y' && answer[0] != 'Y')
				newkey = 0;
		}
		if(newkey)
			getpass(&key, p9pass, 1, 1);
		dosecret = getsecret(newkey, p9pass);
		t = getexpiration(f->keys, u);
		install(f->keys, u, &key, t, newkey);
		if(dosecret && setsecret(KEYDB, u, p9pass) == 0)
			error("error writing Inferno/pop secret");
		newbio = querybio(f->who, u, &a);
		if(newbio)
			wrbio(f->who, &a);
		print("user %s installed for Plan 9\n", u);
		syslog(0, AUTHLOG, "user %s installed for plan 9", u);
	}
	if(which & Securenet){
		f = &fs[Securenet];
		newkey = 1;
		if(exists(f->keys, u)){
			readln("assign new key? [y/n]: ", answer, sizeof answer, 0);
			if(answer[0] != 'y' && answer[0] != 'Y')
				newkey = 0;
		}
		if(newkey){
			memset(&key, 0, sizeof(key));
			genrandom((uint8_t*)key.des, DESKEYLEN);
		}
		if(a.user == 0){
			t = getexpiration(f->keys, u);
			newbio = querybio(f->who, u, &a);
		}
		install(f->keys, u, &key, t, newkey);
		if(newbio)
			wrbio(f->who, &a);
		finddeskey(f->keys, u, key.des);
		print("user %s: SecureNet key: %K\n", u, key.des);
		checksum(key.des, answer);
		print("verify with checksum %s\n", answer);
		print("user %s installed for SecureNet\n", u);
		syslog(0, AUTHLOG, "user %s installed for securenet", u);
	}
	exits(0);
}

void
install(char *db, char *u, Authkey *key, int32_t t, int newkey)
{
	char buf[KEYDBBUF+ANAMELEN+20];
	int fd;

	if(!exists(db, u)){
		sprint(buf, "%s/%s", db, u);
		fd = sys_create(buf, OREAD, 0777|DMDIR);
		if(fd < 0)
			error("can't create user %s: %r", u);
		sys_close(fd);
	}

	if(newkey){
		if(!setkey(db, u, key))
			error("can't set key: %r");
	}

	if(t == -1)
		return;
	sprint(buf, "%s/%s/expire", db, u);
	fd = sys_open(buf, OWRITE);
	if(fd < 0 || fprint(fd, "%ld", t) < 0)
		error("can't write expiration time");
	sys_close(fd);
}

int
exists(char *db, char *u)
{
	char buf[KEYDBBUF+ANAMELEN+6];

	sprint(buf, "%s/%s/expire", db, u);
	if(access(buf, AEXIST) < 0)
		return 0;
	return 1;
}
