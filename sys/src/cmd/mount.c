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
#include <auth.h>

void	usage(void);
void	catch(void *c, char*);

char *keyspec = "";
int mntdevice = '9';

int
amount0(int fd, char *mntpt, int flags, char *aname, char *keyspec)
{
	int rv, afd;
	AuthInfo *ai;

	afd = sys_fauth(fd, aname);
	if(afd >= 0){
		ai = auth_proxy(afd, amount_getkey, "proto=p9any role=client %s", keyspec);
		if(ai != nil)
			auth_freeAI(ai);
		else
			fprint(2, "%s: auth_proxy: %r\n", argv0);
	}
	rv = sys_mount(fd, afd, mntpt, flags, aname, mntdevice);
	if(afd >= 0)
		sys_close(afd);
	return rv;
}

void
setmntdevice(char *dev)
{
	Rune d;
	if(dev[0] != '#')
		usage();
	if(chartorune(&d, ++dev) == 1 && d == Runeerror)
		usage();

	mntdevice = d;
}

void
main(int argc, char *argv[])
{
	char *spec;
	uint32_t flag = 0;
	int qflag = 0;
	int noauth = 0;
	int fd, rv;

	ARGBEGIN{
	case 'a':
		flag |= MAFTER;
		break;
	case 'b':
		flag |= MBEFORE;
		break;
	case 'c':
		flag |= MCREATE;
		break;
	case 'd':
		setmntdevice(EARGF(usage()));
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 'n':
		noauth = 1;
		break;
	case 'q':
		qflag = 1;
		break;
	default:
		usage();
	}ARGEND

	spec = 0;
	if(argc == 2)
		spec = "";
	else if(argc == 3)
		spec = argv[2];
	else
		usage();

	if((flag&MAFTER)&&(flag&MBEFORE))
		usage();

	fd = sys_open(argv[0], ORDWR);
	if(fd < 0){
		if(qflag)
			exits(0);
		fprint(2, "%s: can't open %s: %r\n", argv0, argv[0]);
		exits("open");
	}

	sys_notify(catch);
	if(noauth)
		rv = sys_mount(fd, -1, argv[1], flag, spec, mntdevice);
	else
		rv = amount0(fd, argv[1], flag, spec, keyspec);
	if(rv < 0){
		if(qflag)
			exits(0);
		fprint(2, "%s: mount %s: %r\n", argv0, argv[1]);
		exits("mount");
	}
	exits(0);
}

void
catch(void *x, char *m)
{
	USED(x);
	fprint(2, "mount: %s\n", m);
	exits(m);
}

void
usage(void)
{
	fprint(2, "usage: mount [-a|-b] [-cnq] [-d '#X'] [-k keypattern] /srv/service dir [spec]\n");
	exits("usage");
}
