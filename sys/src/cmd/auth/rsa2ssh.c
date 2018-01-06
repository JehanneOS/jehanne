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
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

void
usage(void)
{
	fprint(2, "usage: auth/rsa2ssh [-2] [-c comment] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	RSApriv *k;
	int ssh2;
	char *comment;

	fmtinstall('B', mpfmt);
	fmtinstall('[', encodefmt);

	ssh2 = 0;
	comment = "";

	ARGBEGIN{
	case 'c':
		comment = EARGF(usage());
		break;
	case '2':
		ssh2 = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getrsakey(argc, argv, 0, nil)) == nil)
		sysfatal("%r");

	if(ssh2) {
		uint8_t buf[8192], *p;

		p = buf;
		p = put4(p, 7);
		p = putn(p, "ssh-rsa", 7);
		p = putmp2(p, k->pub.ek);
		p = putmp2(p, k->pub.n);

		print("ssh-rsa %.*[ %s\n", (int)(p-buf), buf, comment);
	} else {
		print("%d %.10B %.10B %s\n", mpsignif(k->pub.n), k->pub.ek, k->pub.n, comment);
	}

	exits(nil);
}
