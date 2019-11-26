/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include <auth.h>
#include <9P2000.h>
#include "../boot/boot.h"

static Fcall	hdr;

static void
rpc(int fd, int type)
{
	int n, l;
	char buf[128], *p;

	hdr.type = type;
	hdr.tag = NOTAG;
	n = convS2M(&hdr, buf);
	if(jehanne_write(fd, buf, n) != n)
		fatal("write rpc");

	jehanne_print("...");
	p = buf;
	l = 0;
	while(l < 3) {
		n = jehanne_read(fd, p, 3);
		if(n <= 0)
			fatal("read rpc");
		if(n == 2 && l == 0 && buf[0] == 'O' && buf[1] == 'K')
			continue;
		p += n;
		l += n;
	}
	if(convM2S(buf, &hdr, n) == 0){
		jehanne_print("%ux %ux %ux\n", buf[0], buf[1], buf[2]);
		fatal("rpc format");
	}
	if(hdr.tag != NOTAG)
		fatal("rpc tag not NOTAG");
	if(hdr.type == Rerror){
		jehanne_print("error %s;", hdr.ename);
		fatal("remote error");
	}
	if(hdr.type != type+1)
		fatal("not reply");
}

void
nop(int fd)
{
	jehanne_print("nop");
	rpc(fd, Tnop);
}
