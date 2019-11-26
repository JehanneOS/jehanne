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
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "../dhcp.h"
#include "ipconfig.h"

void
pppbinddev(void)
{
	int ac, pid;
	char *av[12];
	Waitmsg *w;

	/* ppp does the binding */
	switch(pid = sys_rfork(RFPROC|RFFDG|RFMEM)){
	case -1:
		sysfatal("can't start ppp: %r");
	case 0:
		ac = 0;
		av[ac++] = "ppp";
		av[ac++] = "-uf";
		av[ac++] = "-p";
		av[ac++] = conf.dev;
		av[ac++] = "-x";
		av[ac++] = conf.mpoint;
		if(conf.baud != nil){
			av[ac++] = "-b";
			av[ac++] = conf.baud;
		}
		av[ac] = nil;
		sys_exec("/bin/ip/ppp", av);
		sys_exec("/ppp", av);
		sysfatal("execing /ppp: %r");
	}

	/* wait for ppp to finish connecting and configuring */
	while((w = wait()) != nil){
		if(w->pid == pid){
			if(w->msg[0] != 0)
				sysfatal("/ppp exited with status: %s", w->msg);
			free(w);
			break;
		}
		free(w);
	}
	if(w == nil)
		sysfatal("/ppp disappeared");

	/* ppp sets up the configuration itself */
	noconfig = 1;
}

