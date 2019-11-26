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
#include <ndb.h>
#include <ip.h>
#include "dat.h"

extern	char *binddir;

int32_t now;
char *blog = "ipboot";
int minlease = MinLease;

void
main(void)
{
	Dir *all;
	int i, nall, fd;
	Binding b;

	fmtinstall('E', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('M', eipfmt);

	fd = sys_open(binddir, OREAD);
	if(fd < 0)
		sysfatal("opening %s: %r", binddir);
	nall = dirreadall(fd, &all);
	if(nall < 0)
		sysfatal("reading %s: %r", binddir);
	sys_close(fd);

	b.boundto = 0;
	b.lease = b.offer = 0;
	now = time(0);
	for(i = 0; i < nall; i++){
		if(parseip(b.ip, all[i].name) == -1 || syncbinding(&b, 0) < 0)
			continue;
		if(b.lease > now)
			print("%I leased by %s until %s", b.ip, b.boundto,
				ctime(b.lease));
	}
	exits(0);
}
