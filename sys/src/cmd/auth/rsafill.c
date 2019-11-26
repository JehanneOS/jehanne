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
	fprint(2, "usage: auth/rsafill [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	RSApriv *key;
	Attr *a;
	char *s;

	fmtinstall('A', _attrfmt);
	fmtinstall('B', mpfmt);
	quotefmtinstall();

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((key = getrsakey(argc, argv, 1, &a)) == nil)
		sysfatal("%r");

	s = smprint("key %A size=%d ek=%B !dk=%B n=%B !p=%B !q=%B !kp=%B !kq=%B !c2=%B\n",
		a, 
		mpsignif(key->pub.n), key->pub.ek,
		key->dk, key->pub.n, key->p, key->q,
		key->kp, key->kq, key->c2);
	if(s == nil)
		sysfatal("smprint: %r");
	jehanne_write(1, s, strlen(s));
	exits(nil);
}
