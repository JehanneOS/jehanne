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
#include <chartypes.h>
#include <authsrv.h>
#include "authcmdlib.h"

void
wrbio(char *file, Acctbio *a)
{
	char buf[1024];
	int i, fd, n;

	fd = sys_open(file, OWRITE);
	if(fd < 0){
		fd = ocreate(file, OWRITE, 0660);
		if(fd < 0)
			error("can't create %s", file);
	}
	if(sys_seek(fd, 0, 2) < 0)
		error("can't seek %s", file);

	if(a->postid == 0)
		a->postid = "";
	if(a->name == 0)
		a->name = "";
	if(a->dept == 0)
		a->dept = "";
	if(a->email[0] == 0)
		a->email[0] = strdup(a->user);

	n = 0;
	n += snprint(buf+n, sizeof(buf)-n, "%s|%s|%s|%s",
		a->user, a->postid, a->name, a->dept);
	for(i = 0; i < Nemail; i++){
		if(a->email[i] == 0)
			break;
		n += snprint(buf+n, sizeof(buf)-n, "|%s", a->email[i]);
	}
	n += snprint(buf+n, sizeof(buf)-n, "\n");

	jehanne_write(fd, buf, n);
	sys_close(fd);
}
