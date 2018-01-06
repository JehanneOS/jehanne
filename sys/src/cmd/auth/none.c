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
#include <auth.h>

char *namespace;

void
usage(void)
{
	fprint(2, "usage: auth/none [-n namespace] [cmd ...]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char cmd[256];
	int fd;

	ARGBEGIN{
	case 'n':
		namespace = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if (rfork(RFENVG|RFNAMEG) < 0)
		sysfatal("can't make new pgrp");

	fd = open("#c/user", OWRITE);
	if (fd < 0)
		sysfatal("can't open #c/user");
	if (write(fd, "none", strlen("none")) < 0)
		sysfatal("can't become none");
	close(fd);

	if (newns("none", namespace) < 0)
		sysfatal("can't build namespace");

	if (argc > 0) {
		strecpy(cmd, cmd+sizeof cmd, argv[0]);
		exec(cmd, &argv[0]);
		if (strncmp(cmd, "/", 1) != 0
		&& strncmp(cmd, "./", 2) != 0
		&& strncmp(cmd, "../", 3) != 0) {
			snprint(cmd, sizeof cmd, "/bin/%s", argv[0]);
			exec(cmd, &argv[0]);
		}
	} else {
		strcpy(cmd, "/bin/rc");
		execl(cmd, cmd, nil);
	}
	sysfatal(cmd);
}
