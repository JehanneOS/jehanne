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
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
#include <u.h>
#include <lib9.h>
#include <auth.h>

void
usage(void)
{
	fprint(2, "Usage:\n\t%s user pass\n\t%s authorization\n", argv0, argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *a, *s;
	int n;

	ARGBEGIN {
	} ARGEND

	switch(argc){
	default:
		usage();
		return;
	case 2:
		s = argv[0];
		a = argv[1];
		break;
	case 1:
		a = argv[0];
		if(cistrncmp(a, "Basic ", 6) == 0)
			a += 6;
		n = strlen(a);
		if((s = malloc(n+1)) == nil)
			sysfatal("out of memory");
		if((n = dec64((uint8_t*)s, n, a, n)) <= 0)
			sysfatal("bad base64");
		s[n] = '\0';
		if((a = strchr(s, ':')) == nil)
			sysfatal("bad format");
		*a++ = '\0';
		break;
	}
	if(*s == '\0')
		sysfatal("empty username");
	if(auth_userpasswd(s, a) == nil)
		sysfatal("bad password");
	print("%s\n", s);
	exits(nil);
}
