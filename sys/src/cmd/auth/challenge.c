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

void
usage(void)
{
	fprint(2, "usage: auth/challenge 'params'\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[128], bufu[128];
	int afd, n;
	AuthInfo *ai;
	AuthRpc *rpc;
	Chalstate *c;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	if((afd = sys_open("/mnt/factotum/rpc", ORDWR)) < 0)
		sysfatal("open /mnt/factotum/rpc: %r");

	rpc = auth_allocrpc(afd);
	if(rpc == nil)
		sysfatal("auth_allocrpc: %r");

	if((c = auth_challenge("%s", argv[0])) == nil)
		sysfatal("auth_challenge: %r");

	print("challenge: %s\n", c->chal);
	print("user:");
	n = jehanne_read(0, bufu, sizeof bufu);
	if(n > 0){
		bufu[n-1] = '\0';
		c->user = bufu;
	}

	print("response: ");
	n = jehanne_read(0, buf, sizeof buf);
	if(n < 0)
		sysfatal("read: %r");
	if(n == 0)
		exits(nil);
	c->nresp = n-1;
	c->resp = buf;
	if((ai = auth_response(c)) == nil)
		sysfatal("auth_response: %r");

	print("%s %s\n", ai->cuid, ai->suid);
}
