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
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include <u.h>
#include <lib9.h>
#include <bio.h>

char *server;
char *status;
int statusonly;

void
usage(void)
{
	fprint(2, "usage: ndb/csquery [/net/cs [addr...]]\n");
	exits("usage");
}

void
query(char *addr)
{
	char buf[128];
	int fd, n;

	fd = sys_open(server, ORDWR);
	if(fd < 0)
		sysfatal("cannot open %s: %r", server);
	if(jehanne_write(fd, addr, strlen(addr)) != strlen(addr)){
		if(!statusonly)
			fprint(2, "translating %s: %r\n", addr);
		status = "errors";
		sys_close(fd);
		return;
	}
	if(!statusonly){
		sys_seek(fd, 0, 0);
		while((n = jehanne_read(fd, buf, sizeof(buf)-1)) > 0){
			buf[n] = 0;
			print("%s\n", buf);
		}
	}
	sys_close(fd);
}

void
main(int argc, char **argv)
{
	char *p;
	int i;
	Biobuf in;

	ARGBEGIN{
	case 's':
		statusonly = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		server = argv[0];
	else
		server = "/net/cs";

	if(argc > 1){
		for(i=1; i<argc; i++)
			query(argv[i]);
		exits(status);
	}

	Binit(&in, 0, OREAD);
	for(;;){
		print("> ");
		p = Brdline(&in, '\n');
		if(p == 0)
			break;
		p[Blinelen(&in)-1] = 0;
		query(p);
	}
	exits(nil);
}
