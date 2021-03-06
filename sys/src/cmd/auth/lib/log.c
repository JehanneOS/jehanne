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
#include <authsrv.h>
#include <bio.h>
#include "authcmdlib.h"

static void
record(char *db, char *user, char *msg)
{
	char buf[Maxpath];
	int fd;

	snprint(buf, sizeof buf, "%s/%s/log", db, user);
	fd = sys_open(buf, OWRITE);
	if(fd < 0)
		return;
	jehanne_write(fd, msg, strlen(msg));
	sys_close(fd);
	return;
}

void
logfail(char *user)
{
	if(!user)
		return;
	record(KEYDB, user, "bad");
	record(NETKEYDB, user, "bad");
}

void
succeed(char *user)
{
	if(!user)
		return;
	record(KEYDB, user, "good");
	record(NETKEYDB, user, "good");
}

void
fail(char *user)
{
	logfail(user);
	exits("failure");
}
