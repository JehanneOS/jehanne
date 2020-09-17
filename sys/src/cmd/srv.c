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
#include <bio.h>
#include <auth.h>

char	*dest = "system";
int	mountflag = MREPL;

void	error(char *);
void	rpc(int, int);
void	post(char*, int);
void	mountfs(char*, int);
int	doauth = 1;

void
usage(void)
{
	fprint(2, "usage: %s [-abcCm] [net!]host [srvname [mtpt]]\n", argv0);
	fprint(2, "    or %s -e [-abcCm] command [srvname [mtpt]]\n", argv0);

	exits("usage");
}

void
ignore(void *a, char *c)
{
	USED(a);
	if(strcmp(c, "alarm") == 0){
		fprint(2, "srv: timeout establishing connection to %s\n", dest);
		exits("timeout");
	}
	if(strstr(c, "write on closed pipe") == 0){
		fprint(2, "write on closed pipe\n");
		sys_noted(NCONT);
	}
	sys_noted(NDFLT);
}

int
connectcmd(char *cmd)
{
	int p[2];

	if(pipe(p) < 0)
		return -1;
	switch(fork()){
	case -1:
		fprint(2, "fork failed: %r\n");
		sys__exits("exec");
	case 0:
		sys_rfork(RFNOTEG);
		dup(p[0], 0);
		dup(p[0], 1);
		sys_close(p[1]);
		execl("/cmd/rc", "rc", "-c", cmd, nil);
		fprint(2, "exec failed: %r\n");
		sys__exits("exec");
	default:
		sys_close(p[0]);
		return p[1];
	}
}

void
main(int argc, char *argv[])
{
	int fd, doexec;
	char *srv, *mtpt;
	char dir[1024];
	char err[ERRMAX];
	char *p, *p2;
	int domount, reallymount, try, sleeptime;

	sys_notify(ignore);

	domount = 0;
	reallymount = 0;
	doexec = 0;
	sleeptime = 0;

	ARGBEGIN{
	case 'a':
		mountflag |= MAFTER;
		domount = 1;
		reallymount = 1;
		break;
	case 'b':
		mountflag |= MBEFORE;
		domount = 1;
		reallymount = 1;
		break;
	case 'c':
		mountflag |= MCREATE;
		domount = 1;
		reallymount = 1;
		break;
	case 'e':
		doexec = 1;
		break;
	case 'm':
		domount = 1;
		reallymount = 1;
		break;
	case 'n':
		doauth = 0;
		break;
	case 'q':
		domount = 1;
		reallymount = 0;
		break;
	case 'r':
		/* deprecated -r flag; ignored for compatibility */
		break;
	case 's':
		sleeptime = atoi(EARGF(usage()));
		break;
	default:
		usage();
		break;
	}ARGEND

	if((mountflag&MAFTER)&&(mountflag&MBEFORE))
		usage();

	switch(argc){
	case 1:	/* calculate srv and mtpt from address */
		p = strrchr(argv[0], '/');
		p = p ? p+1 : argv[0];
		srv = smprint("/srv/%s", p);
		p2 = strchr(p, '!');
		p2 = p2 ? p2+1 : p;
		mtpt = smprint("/n/%s", p2);
		break;
	case 2:	/* calculate mtpt from address, srv given */
		srv = smprint("/srv/%s", argv[1]);
		p = strrchr(argv[0], '/');
		p = p ? p+1 : argv[0];
		p2 = strchr(p, '!');
		p2 = p2 ? p2+1 : p;
		mtpt = smprint("/n/%s", p2);
		break;
	case 3:	/* srv and mtpt given */
		domount = 1;
		reallymount = 1;
		srv = smprint("/srv/%s", argv[1]);
		mtpt = smprint("%s", argv[2]);
		break;
	default:
		srv = mtpt = nil;
		usage();
	}

	try = 0;
	dest = *argv;
Again:
	try++;

	if(access(srv, AEXIST) == 0){
		if(domount){
			fd = sys_open(srv, ORDWR);
			if(fd >= 0)
				goto Mount;
			sys_remove(srv);
		}
		else{
			fprint(2, "srv: %s already exists\n", srv);
			exits(0);
		}
	}

	sys_alarm(10000);
	if(doexec)
		fd = connectcmd(dest);
	else{
		dest = netmkaddr(dest, 0, "9fs");
		fd = dial(dest, 0, dir, 0);
	}
	if(fd < 0) {
		fprint(2, "srv: dial %s: %r\n", dest);
		exits("dial");
	}
	sys_alarm(0);

	if(sleeptime){
		fprint(2, "sleep...");
		sleep(sleeptime*1000);
	}

	post(srv, fd);

Mount:
	if(domount == 0 || reallymount == 0)
		exits(0);

	if((!doauth && sys_mount(fd, -1, mtpt, mountflag, "", '9') < 0)
	|| (doauth && amount(fd, mtpt, mountflag, "") < 0)){
		err[0] = 0;
		sys_errstr(err, sizeof err);
		if(strstr(err, "Hangup") || strstr(err, "hungup") || strstr(err, "timed out")){
			sys_remove(srv);
			if(try == 1)
				goto Again;
		}
		fprint(2, "srv %s: mount failed: %s\n", dest, err);
		exits("mount");
	}
	exits(0);
}

void
post(char *srv, int fd)
{
	int f;
	char buf[128];

	fprint(2, "post...\n");
	f = ocreate(srv, OWRITE, 0666);
	if(f < 0){
		sprint(buf, "sys_create(%s)", srv);
		error(buf);
	}
	sprint(buf, "%d", fd);
	if(jehanne_write(f, buf, strlen(buf)) != strlen(buf))
		error("write");
}

void
error(char *s)
{
	fprint(2, "srv %s: %s: %r\n", dest, s);
	exits("srv: error");
}
