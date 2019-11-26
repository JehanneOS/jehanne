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
#include <bio.h>
#include <mp.h>
#include <libsec.h>

void
usage(void)
{
	fprint(2, "auth/pemencode section [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *buf, *cbuf;
	int fd;
	int32_t n, tot;
	int len;
	char *tag, *file;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 1 && argc != 2)
		usage();

	tag = argv[0];
	if(argc == 2)
		file = argv[1];
	else
		file = "#d/0";

	if((fd = sys_open(file, OREAD)) < 0)
		sysfatal("open %s: %r", file);
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
	len = 2*tot+3;
	cbuf = malloc(len);
	if(cbuf == nil)
		sysfatal("malloc: %r");
	len = enc64(cbuf, len, (uint8_t*)buf, tot);
	print("-----BEGIN %s-----\n", tag);
	while(len > 0){
		print("%.64s\n", cbuf);
		cbuf += 64;
		len -= 64;
	}
	print("-----END %s-----\n", tag);
	exits(0);
}
