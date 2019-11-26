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
	fprint(2, "usage: auth/rsa2pub [file]\n");
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

	if((key = getrsakey(argc, argv, 0, &a)) == nil)
		sysfatal("%r");

	s = smprint("key %A size=%d ek=%B n=%B\n",
		a, 
		mpsignif(key->pub.n), key->pub.ek, key->pub.n);
	if(s == nil)
		sysfatal("smprint: %r");
	jehanne_write(1, s, strlen(s));
	exits(nil);
}
