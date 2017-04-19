/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <lib9.h>

#include "console.h"

int linecontrol;
int blind;
int crnl;

static void
usage(void)
{
	fprint(2, "usage: %s [-d dbgfile] [-s srvname] comfile [program [args]]\n", argv0);
	exits("usage");
}
static void
opencom(char *file)
{
	int fd;

	if((fd = open(file, ORDWR)) <= 0)
		sysfatal("open: %r");
	dup(fd, 0);
	dup(fd, 1);
	close(fd);
}
void
main(int argc, char *argv[])
{
	int fd, devmnt;
	char *srv = nil;

	blind = 0;
	linecontrol = 1;
	crnl = 1;
	ARGBEGIN{
	case 'd':
		enabledebug(EARGF(usage()));
		break;
	case 's':
		srv = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if(srv && argc != 1)
		usage();
	if(argc < 2 && srv == nil)
		usage();

	opencom(argv[0]);

	fd = servecons(passthrough, passthrough, &devmnt);

	if(srv){
		debug("%s (%d): posting %d\n", argv0, getpid(), fd);
		post(srv, fd);
		exits(0);
	} else {
		debug("%s %d: mounting cons for %s\n", argv0, getpid(), argv[0]);
		if(mount(fd, -1, "/dev", MBEFORE, "", devmnt) == -1)
			sysfatal("mount (%s): %r", argv[0]);

		debug("%s (%d): all services started, ready to exec(%s)\n", argv0, getpid(), argv[0]);

		/* become the requested program */
		rfork(RFNOTEG|RFREND|RFCFDG);

		fd = open("/dev/cons", OREAD);
		fd = open("/dev/cons", OWRITE);
		if(dup(fd, 2) != 2)
			sysfatal("bad FDs: %r");
		exec(argv[1], argv+1);
		sysfatal("exec %s: %r", argv[1]);
	}
}
