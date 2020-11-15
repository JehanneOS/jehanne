/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <lib9.h>
#include <envvars.h>
#include <auth.h>

int verbose;
int trusted;

void
usage(void)
{
	fprint(2, "usage: ip/listen1 [-tv] address cmd args...\n");
	exits("usage");
}

void
becomenone(void)
{
	int fd;

	fd = sys_open("#c/user", OWRITE);
	if(fd < 0 || jehanne_write(fd, "none", strlen("none")) < 0)
		sysfatal("can't become none: %r");
	sys_close(fd);
	if(newns("none", nil) < 0)
		sysfatal("can't build namespace: %r");
}

char*
remoteaddr(char *dir)
{
	static char buf[128];
	char *p;
	int n, fd;

	snprint(buf, sizeof buf, "%s/remote", dir);
	fd = sys_open(buf, OREAD);
	if(fd < 0)
		return "";
	n = jehanne_read(fd, buf, sizeof(buf));
	sys_close(fd);
	if(n > 0){
		buf[n] = 0;
		p = strchr(buf, '!');
		if(p)
			*p = 0;
		return buf;
	}
	return "";
}

void
main(int argc, char **argv)
{
	char data[60], dir[40], ndir[40];
	int ctl, nctl, fd;

	ARGBEGIN{
	default:
		usage();
	case 't':
		trusted = 1;
		break;
	case 'v':
		verbose = 1;
		break;
	}ARGEND

	if(argc < 2)
		usage();

	if(!verbose){
		sys_close(1);
		fd = sys_open("/dev/null", OWRITE);
		if(fd != 1){
			dup(fd, 1);
			sys_close(fd);
		}
	}

	if(!trusted)
		becomenone();

	print("listen started\n");
	ctl = announce(argv[0], dir);
	if(ctl < 0)
		sysfatal("announce %s: %r", argv[0]);

	for(;;){
		nctl = listen(dir, ndir);
		if(nctl < 0)
			sysfatal("listen %s: %r", argv[0]);

		switch(sys_rfork(RFFDG|RFPROC|RFNOWAIT|RFENVG|RFNAMEG|RFNOTEG)){
		case -1:
			reject(nctl, ndir, "host overloaded");
			sys_close(nctl);
			continue;
		case 0:
			fd = accept(nctl, ndir);
			if(fd < 0){
				fprint(2, "accept %s: can't open  %s/data: %r\n",
					argv[0], ndir);
				sys__exits(0);
			}
			print("incoming call for %s from %s in %s\n", argv[0],
				remoteaddr(ndir), ndir);
			fprint(nctl, "keepalive");
			sys_close(ctl);
			sys_close(nctl);
			putenv("net", ndir);
			snprint(data, sizeof data, "%s/data", ndir);
			sys_bind(data, "/dev/cons", MREPL);
			dup(fd, 0);
			dup(fd, 1);
			dup(fd, 2);
			sys_close(fd);
			sys_exec(argv[1], (const char **)(argv+1));
			if(argv[1][0] != '/')
				sys_exec(smprint("/cmd/%s", argv[1]), (const char**)(argv+1));
			fprint(2, "%s: exec: %r\n", argv0);
			exits(nil);
		default:
			sys_close(nctl);
			break;
		}
	}
}
