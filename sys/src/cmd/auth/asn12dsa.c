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
#include <bio.h>
#include <mp.h>
#include <libsec.h>

void
usage(void)
{
	fprint(2, "auth/asn12dsa [-t tag] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *s;
	uint8_t *buf;
	int fd;
	int32_t n, tot;
	char *tag;
	DSApriv *key;

	fmtinstall('B', mpfmt);

	tag = nil;
	ARGBEGIN{
	case 't':
		tag = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0 && argc != 1)
		usage();

	fd = 0;
	if(argc == 1){
		if((fd = sys_open(*argv, OREAD)) < 0)
			sysfatal("open %s: %r", *argv);
	}

	buf = nil;
	tot = 0;
	for(;;){
		buf = realloc(buf, tot+8192);
		if(buf == nil)
			sysfatal("realloc: %r");
		if((n = jehanne_read(fd, buf+tot, 8192)) < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		tot += n;
	}

	key = asn1toDSApriv(buf, tot);
	if(key == nil)
		sysfatal("couldn't parse asn1 key");

	s = smprint("key proto=dsa %s%sp=%B q=%B alpha=%B key=%B !secret=%B\n",
		tag ? tag : "", tag ? " " : "",
		key->pub.p, key->pub.q, key->pub.alpha, key->pub.key,
		key->secret);
	if(s == nil)
		sysfatal("smprint: %r");
	jehanne_write(1, s, strlen(s));
	exits(0);
}
