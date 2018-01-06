/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include <u.h>
#include <lib9.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

void
usage(void)
{
	fprint(2, "usage: auth/dsa2ssh [-c comment] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	DSApriv *k;
	char *comment;
	uint8_t buf[8192], *p;
	
	fmtinstall('B', mpfmt);
	fmtinstall('[', encodefmt);
	comment = "";
	ARGBEGIN{
	case 'c':
		comment = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getdsakey(argc, argv, 0, nil)) == nil)
		sysfatal("%r");

	p = buf;
	p = put4(p, 7);
	p = putn(p, "ssh-dss", 7);
	p = putmp2(p, k->pub.p);
	p = putmp2(p, k->pub.q);
	p = putmp2(p, k->pub.alpha);
	p = putmp2(p, k->pub.key);
	print("ssh-dss %.*[ %s\n", (int)(p - buf), buf, comment);
	exits(nil);
}
