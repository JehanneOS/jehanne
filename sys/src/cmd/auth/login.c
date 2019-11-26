/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <lib9.h>
#include <envvars.h>
#include <auth.h>
#include <authsrv.h>
#include <bio.h>
#include <ndb.h>

char *authdom;

void
readln(char *prompt, char *line, int len, int raw)
{
	char *p;
	int fdin, fdout, ctl, n, nr;

	fdin = sys_open("/dev/cons", OREAD);
	fdout = sys_open("/dev/cons", OWRITE);
	fprint(fdout, "%s", prompt);
	if(raw){
		ctl = sys_open("/dev/consctl", OWRITE);
		if(ctl < 0){
			fprint(2, "login: couldn't set raw mode");
			exits("readln");
		}
		jehanne_write(ctl, "rawon", 5);
	} else
		ctl = -1;
	nr = 0;
	p = line;
	for(;;){
		n = jehanne_read(fdin, p, 1);
		if(n < 0){
			sys_close(ctl);
			sys_close(fdin);
			sys_close(fdout);
			fprint(2, "login: can't read cons");
			exits("readln");
		}
		if(*p == 0x7f)
			exits(0);
		if(n == 0 || *p == '\n' || *p == '\r'){
			*p = '\0';
			if(raw){
				jehanne_write(ctl, "rawoff", 6);
				jehanne_write(fdout, "\n", 1);
			}
			sys_close(ctl);
			sys_close(fdin);
			sys_close(fdout);
			return;
		}
		if(*p == '\b'){
			if(nr > 0){
				nr--;
				p--;
			}
		}else{
			nr++;
			p++;
		}
		if(nr == len){
			fprint(fdout, "line too int32_t; try again\n");
			nr = 0;
			p = line;
		}
	}
}

void
setenv(char *var, char *val)
{
	int fd;
	char buf[128+4];

	snprint(buf, sizeof(buf), "#e/%s", var);
	fd = ocreate(buf, OWRITE, 0644);
	if(fd < 0)
		print("init: can't open %s\n", buf);
	else{
		fprint(fd, val);
		sys_close(fd);
	}
}

/*
 *  become the authenticated user
 */
void
chuid(AuthInfo *ai)
{
	int rv, fd;

	/* change uid */
	fd = sys_open("#¤/capuse", OWRITE);
	if(fd < 0)
		sysfatal("can't change uid: %r");
	rv = jehanne_write(fd, ai->cap, strlen(ai->cap));
	sys_close(fd);
	if(rv < 0)
		sysfatal("can't change uid: %r");
}

/*
 *  mount a factotum
 */
void
mountfactotum(char *srvname)
{
	int fd;

	/* mount it */
	fd = sys_open(srvname, ORDWR);
	if(fd < 0)
		sysfatal("opening factotum: %r");
	sys_mount(fd, -1, "/mnt", MBEFORE, "", '9');
	sys_close(fd);
}

/*
 * find authdom
 */
char*
getauthdom(void)
{
	char *sysname, *s;
	Ndbtuple *t, *p;

	if(authdom != nil)
		return authdom;

	sysname = getenv(ENV_SYSNAME);
	if(sysname == nil)
		return strdup("cs.bell-labs.com");

	s = "authdom";
	t = csipinfo(nil, "sys", sysname, &s, 1);
	free(sysname);
	for(p = t; p != nil; p = p->entry)
		if(strcmp(p->attr, s) == 0){
			authdom = strdup(p->val);
			break;
		}
	ndbfree(t);
fprint(2, "authdom=%s\n", authdom);
	return authdom;
}

/*
 *  start a new factotum and pass it the username and password
 */
void
startfactotum(char *user, char *password, char *srvname)
{
	int fd;

	strcpy(srvname, "/srv/factotum.XXXXXXXXXXX");
	mktemp(srvname);

	switch(fork()){
	case -1:
		sysfatal("can't start factotum: %r");
	case 0:
		execl("/boot/factotum", "loginfactotum", "-ns", srvname+5, nil);
		sysfatal("starting factotum: %r");
		break;
	}

	/* wait for agent to really be there */
	while(access(srvname, AEXIST) < 0)
		sleep(250);

	/* mount it */
	mountfactotum(srvname);

	/* write in new key */
	fd = sys_open("/mnt/factotum/ctl", ORDWR);
	if(fd < 0)
		sysfatal("opening factotum: %r");
	fprint(fd, "key proto=p9sk1 dom=%s user=%q !password=%q", getauthdom(), user, password);
	sys_close(fd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-a authdom] user\n", argv0);
	exits("");
}

void
main(int argc, char *argv[])
{
	char pass[ANAMELEN];
	char buf[2*ANAMELEN];
	char home[2*ANAMELEN];
	char srvname[2*ANAMELEN];
	char *user, *sysname, *tz, *cputype, *service;
	AuthInfo *ai;

	ARGBEGIN{
	case 'a':
		authdom = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(argc != 1)
		usage();

	sys_rfork(RFENVG|RFNAMEG);

	service = getenv(ENV_SERVICE);
	if(strcmp(service, "cpu") == 0)
		fprint(2, "login: warning: running on a cpu server!\n");
	if(argc != 1){
		fprint(2, "usage: login username\n");
		exits("usage");
	}
	user = argv[0];
	memset(pass, 0, sizeof(pass));
	readln("Password: ", pass, sizeof(pass), 1);

	/* authenticate */
	ai = auth_userpasswd(user, pass);
	if(ai == nil || ai->cap == nil)
		sysfatal("login incorrect");

	/* change uid */
	chuid(ai);

	/* start a new factotum and hand it a new key */
	startfactotum(user, pass, srvname);

	/* set up new namespace */
	newns(ai->cuid, nil);
	auth_freeAI(ai);

	/* remount the factotum */
	mountfactotum(srvname);

	/* set up a new environment */
	cputype = getenv(ENV_CPUTYPE);
	sysname = getenv(ENV_SYSNAME);
	tz = getenv("timezone");
	sys_rfork(RFCENVG);
	setenv(ENV_SERVICE, "con");
	setenv(ENV_USER, user);
	snprint(home, sizeof(home), "/usr/%s", user);
	setenv(ENV_HOME, home);
	setenv(ENV_CPUTYPE, cputype);
	setenv(ENV_OBJTYPE, cputype);
	if(sysname != nil)
		setenv(ENV_SYSNAME, sysname);
	if(tz != nil)
		setenv("timezone", tz);

	/* go to new home directory */
	if(chdir(home) < 0)
		chdir("/");

	/* read profile and start interactive rc */
	execl("/bin/rc", "rc", "-li", nil);
	exits(0);
}
